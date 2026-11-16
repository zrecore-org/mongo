// MP14a POC: a MongoDB extension (C ABI only, no mongo/base) registering one transform stage, $meshVolume.
// Spec: {$meshVolume: {input: "mesh", output: "meshVolume", batchSize: 1024, plant: "...", spinMs: N, probe: "..."}}
// Per input document it reads a BinData field holding a packed triangle mesh and appends
//   output: {exact: "<reduced rational p/q>", approx: <binary64 correctly rounded>}  or  output: {error: {code, reason}}
// Batching: when its buffer is empty, get_next() pulls up to batchSize documents from the source stage (one host get_next per document, forced by
// the API), makes ONE computeBatch() call over all of them, then serves the results one at a time.
// Test-only knobs (plant / probe) exist to exercise the boundary in the fake host: planted throw / segfault / timeouts, and counters / vulkan probes.
// Not exported: everything except the two loading symbols (see exports.list / -fvisibility=hidden).
#include "mongo/db/extension/public/api.h"

#include "bson_min.h"
#include "meshvol_core.h"
#include "meshcontains_core.h"
#ifdef MESHVOL_GPU
#include "gpu_pip.h"
#endif

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <deque>
#include "platform_compat.h"
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef MESHVOL_VULKAN_PROBE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#endif

#define EXPORT MESHVOL_EXPORT

// Symbol-conflict probe: a plain global function that a host could also define (see host.cpp). With default visibility it would be an exported,
// interposable symbol; with -fvisibility=hidden + the export list it is not.
__attribute__((noinline)) int collide_me() { return 1; }

namespace {
using std::string;
string configDoc();  // MP14c, defined below
const char* engineName();
struct Spec;
bool gpuEligible(const Spec& spec, size_t nPoints);   // MP14d, defined after the configuration
struct MeshEntry;
int containsGpu(MeshEntry& me, const std::vector<double>& pts, const Spec& spec, string* verdicts, bool (*cancel)(void*), void* cancelArg);   // 1 done, 0 fall back to the CPU path, 2 cancelled

// ------------------------------------------------------------------ counters (reported through the "counters" probe)
std::atomic<long> g_computeCalls{0}, g_sourcePulls{0}, g_throwsCaught{0}, g_maxBatch{0}, g_liveBufs{0}, g_liveStatuses{0}, g_liveObjs{0}, g_docsOut{0}, g_getNextCalls{0};
const MongoExtensionHostServices* g_hostServices = nullptr;

// ------------------------------------------------------------------ statuses
struct ExtErr {  // thrown internally and converted at the boundary
    int32_t code;
    string reason;
};

MongoExtensionStatus* cloneOk(const MongoExtensionStatus* s, MongoExtensionStatus** out);
const MongoExtensionStatusVTable kOkVt = {
    [](MongoExtensionStatus*) {},  // singleton: destroy is a no-op, mirroring shared/extension_status.h (ExtensionStatusOK)
    [](const MongoExtensionStatus*) -> int32_t { return MONGO_EXTENSION_STATUS_OK; },
    [](const MongoExtensionStatus*) -> MongoExtensionByteView { return {nullptr, 0}; },
    [](MongoExtensionStatus*, int32_t) {},
    [](MongoExtensionStatus* s, MongoExtensionByteView) -> MongoExtensionStatus* { return s; },
    &cloneOk,
};
struct OkStatus : MongoExtensionStatus { OkStatus() : MongoExtensionStatus{&kOkVt} {} };
OkStatus g_ok;
MongoExtensionStatus* cloneOk(const MongoExtensionStatus*, MongoExtensionStatus** out) { *out = &g_ok; return &g_ok; }

struct ErrStatus : MongoExtensionStatus {
    int32_t code;
    string reason;
    ErrStatus(int32_t c, string r);
    static const MongoExtensionStatusVTable vt;
};
const MongoExtensionStatusVTable ErrStatus::vt = {
    [](MongoExtensionStatus* s) { g_liveStatuses--; delete static_cast<ErrStatus*>(s); },
    [](const MongoExtensionStatus* s) -> int32_t { return static_cast<const ErrStatus*>(s)->code; },
    [](const MongoExtensionStatus* s) -> MongoExtensionByteView {
        auto& r = static_cast<const ErrStatus*>(s)->reason;
        return {reinterpret_cast<const uint8_t*>(r.data()), r.size()};
    },
    [](MongoExtensionStatus* s, int32_t c) { static_cast<ErrStatus*>(s)->code = c; },
    [](MongoExtensionStatus* s, MongoExtensionByteView v) -> MongoExtensionStatus* {
        static_cast<ErrStatus*>(s)->reason.assign(reinterpret_cast<const char*>(v.data), v.len);
        return &g_ok;
    },
    [](const MongoExtensionStatus* s, MongoExtensionStatus** out) -> MongoExtensionStatus* {
        auto* e = static_cast<const ErrStatus*>(s);
        *out = new ErrStatus(e->code, e->reason);
        return &g_ok;
    },
};
ErrStatus::ErrStatus(int32_t c, string r) : MongoExtensionStatus{&vt}, code(c), reason(std::move(r)) { g_liveStatuses++; }

MongoExtensionStatus* makeErr(int32_t code, string reason) { return new ErrStatus(code, std::move(reason)); }

// Every vtable entry that returns a status runs its body through guard(): no C++ exception crosses the C boundary.
// -DNO_BOUNDARY_CATCH builds the deliberately broken variant used to show what an escaping exception does.
template <class F>
MongoExtensionStatus* guard(F&& f) {
#ifdef NO_BOUNDARY_CATCH
    f();
    return &g_ok;
#else
    try {
        f();
        return &g_ok;
    } catch (const ExtErr& e) {
        return makeErr(e.code, e.reason);
    } catch (const std::exception& e) {
        g_throwsCaught++;
        return makeErr(MONGO_EXTENSION_STATUS_RUNTIME_ERROR, string("caught C++ exception at the extension boundary: ") + e.what());
    } catch (...) {
        g_throwsCaught++;
        return makeErr(MONGO_EXTENSION_STATUS_RUNTIME_ERROR, "caught unknown C++ exception at the extension boundary");
    }
#endif
}
// Consumes a status returned by the host: OK -> nothing, otherwise throws ExtErr and destroys the status.
void consume(MongoExtensionStatus* s) {
    if (!s) return;
    const int32_t c = s->vtable->get_code(s);
    if (c == MONGO_EXTENSION_STATUS_OK) { s->vtable->destroy(s); return; }
    auto v = s->vtable->get_reason(s);
    ExtErr e{c, string(reinterpret_cast<const char*>(v.data), v.len)};
    s->vtable->destroy(s);
    throw e;
}

// ------------------------------------------------------------------ byte buffers
struct BufImpl : MongoExtensionByteBuf {
    string data;
    explicit BufImpl(string d) : MongoExtensionByteBuf{&vt}, data(std::move(d)) { g_liveBufs++; }
    static const MongoExtensionByteBufVTable vt;
};
const MongoExtensionByteBufVTable BufImpl::vt = {
    [](MongoExtensionByteBuf* b) { g_liveBufs--; delete static_cast<BufImpl*>(b); },
    [](const MongoExtensionByteBuf* b) -> MongoExtensionByteView {
        auto& d = static_cast<const BufImpl*>(b)->data;
        return {reinterpret_cast<const uint8_t*>(d.data()), d.size()};
    },
};

// ------------------------------------------------------------------ stage spec
struct Spec {
    bool contains = false;  // $meshContains (MP14d) instead of $meshVolume
    string input = "mesh", output = "meshVolume", points = "points", plant, probe;
    uint32_t batchSize = 1024;
    uint32_t gpuMinPoints = 250000;   // $meshContains: the device path is considered only for at least this many points in one document
    bool meshCache = true;            // $meshContains: reuse the parsed mesh, its BVH and device tree across documents with byte-identical meshes
    string engine = "auto";          // $meshContains: "auto" (device when enabled, ready and large enough) or "cpu" (never the device)
    int spinMs = 0;
    string raw;  // the validated sub-document, for serialize / clone

    const char* stageName() const { return contains ? "$meshContains" : "$meshVolume"; }
    static Spec fromStageBson(const uint8_t* d, size_t len) {
        if (!bm::validateDoc(d, len)) throw ExtErr{11, "mesh stage: stage BSON is malformed"};
        bm::Elem e;
        const bool contains = bm::findField(d, len, "$meshContains", &e);
        if (!contains && !bm::findField(d, len, "$meshVolume", &e)) throw ExtErr{12, "mesh stage: unknown stage name"};
        if (e.type != bm::kDoc) throw ExtErr{12, string(contains ? "$meshContains" : "$meshVolume") + ": the stage specification must be a document"};
        return fromSpec(e.val, e.vlen, contains);
    }
    static Spec fromSpec(const uint8_t* d, size_t len, bool contains = false) {
        if (!bm::validateDoc(d, len)) throw ExtErr{11, "$meshVolume: specification is malformed BSON"};
        Spec s;
        s.contains = contains;
        if (contains) s.output = "contains";
        s.raw.assign(reinterpret_cast<const char*>(d), len);
        const uint8_t* p = d + 4;
        const uint8_t* end = d + len - 1;
        while (p < end) {
            bm::Elem e;
            bm::readElem(p, end, &e);
            p = e.end;
            auto strVal = [&]() -> string {
                if (e.type != bm::kString) throw ExtErr{13, "$meshVolume: field '" + string(e.name) + "' must be a string"};
                return string(reinterpret_cast<const char*>(e.val + 4), e.vlen - 5);
            };
            auto numVal = [&]() -> long {
                if (e.type == bm::kInt32) { int32_t v; std::memcpy(&v, e.val, 4); return v; }
                if (e.type == bm::kInt64) { int64_t v; std::memcpy(&v, e.val, 8); return (long)v; }
                if (e.type == bm::kDouble) { double v; std::memcpy(&v, e.val, 8); return (long)v; }
                throw ExtErr{14, "$meshVolume: field '" + string(e.name) + "' must be a number"};
            };
            if (e.name == "input") s.input = strVal();
            else if (e.name == "output") s.output = strVal();
            else if (s.contains && e.name == "points") s.points = strVal();
            else if (s.contains && e.name == "meshCache") { if (e.type != bm::kBool) throw ExtErr{21, "$meshContains: meshCache must be a boolean"}; s.meshCache = e.val[0] != 0; }
            else if (s.contains && e.name == "engine") { s.engine = strVal(); if (s.engine != "auto" && s.engine != "cpu") throw ExtErr{19, "$meshContains: engine must be \"auto\" or \"cpu\""}; }
            else if (s.contains && e.name == "gpuMinPoints") { long v = numVal(); if (v < 1 || v > (1L << 30)) throw ExtErr{20, "$meshContains: gpuMinPoints must be in [1, 2^30]"}; s.gpuMinPoints = (uint32_t)v; }
            else if (e.name == "plant") s.plant = strVal();
            else if (e.name == "probe") s.probe = strVal();
            else if (e.name == "batchSize") {
                long v = numVal();
                if (v < 1 || v > (1 << 24)) throw ExtErr{15, "$meshVolume: batchSize must be in [1, 16777216]"};
                s.batchSize = (uint32_t)v;
            } else if (e.name == "spinMs") {
                long v = numVal();
                if (v < 0 || v > 5000) throw ExtErr{16, "$meshVolume: spinMs must be in [0, 5000] (the POC caps planted spins)"};
                s.spinMs = (int)v;
            } else throw ExtErr{17, "$meshVolume: unknown field '" + string(e.name) + "'"};
        }
        if (s.input.empty() || s.output.empty()) throw ExtErr{18, "$meshVolume: input/output must be non-empty"};
        return s;
    }
    string toStageBson() const {
        bm::Builder b;
        b.head(bm::kDoc, stageName());
        b.b.append(raw);
        return b.finish();
    }
};

// ------------------------------------------------------------------ the batch compute (the "GPU-shaped" entry point: one call, N documents)
struct DocIn { const uint8_t* data; size_t len; };
struct DocOut {
    int code = meshvol::kOk;
    meshvol::Result r;
    string probeDoc;  // when a probe replaced the volume
    string verdicts;  // $meshContains: one char per point: 'O' out, 'I' in, 'B' on the boundary
};
using CancelFn = bool (*)(void*);

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

string vulkanProbe();
string countersDoc();

// ------------------------------------------------------------------ $meshContains (MP14d): points input and the CPU path
// Points: a BSON array of [x, y, z] arrays (double, int32 or int64 up to 2^53), or a BinData subtype 0 with n*3 little-endian binary64. Returns kOk or kBadPoints.
int readPoints(const bm::Elem& e, std::vector<double>* out) {
    if (e.type == bm::kBinData) {
        if (e.val[4] != 0) return meshvol::kBadSubtype;
        const size_t n = e.vlen - 5;
        if (n % 24 != 0) return meshvol::kBadPoints;
        out->resize(n / 8);
        std::memcpy(out->data(), e.val + 5, n);
        for (double d : *out) if (!std::isfinite(d)) return meshvol::kNonFinite;
        return meshvol::kOk;
    }
    if (e.type != bm::kArray) return meshvol::kBadPoints;
    const uint8_t* p = e.val + 4;
    const uint8_t* end = e.val + e.vlen - 1;
    while (p < end) {
        bm::Elem pt;
        if (!bm::readElem(p, end, &pt)) return meshvol::kBadPoints;
        p = pt.end;
        if (pt.type != bm::kArray) return meshvol::kBadPoints;
        const uint8_t* q = pt.val + 4;
        const uint8_t* qe = pt.val + pt.vlen - 1;
        int k = 0;
        while (q < qe) {
            bm::Elem c;
            if (!bm::readElem(q, qe, &c)) return meshvol::kBadPoints;
            q = c.end;
            double v;
            if (c.type == bm::kDouble) std::memcpy(&v, c.val, 8);
            else if (c.type == bm::kInt32) { int32_t i; std::memcpy(&i, c.val, 4); v = i; }
            else if (c.type == bm::kInt64) { int64_t i; std::memcpy(&i, c.val, 8); if (i > (1LL << 53) || i < -(1LL << 53)) return meshvol::kBadPoints; v = (double)i; }
            else return meshvol::kBadPoints;
            if (!std::isfinite(v) || k >= 3) return meshvol::kBadPoints;
            out->push_back(v);
            k++;
        }
        if (k != 3) return meshvol::kBadPoints;
    }
    return meshvol::kOk;
}
// CPU path: exact verdicts for every point. This is the default and the oracle for any other path.
// ------------------------------------------------------------------ MP14d: mesh cache (dedup across documents)
// Documents whose mesh bytes are identical share one parsed mesh, one CPU BVH and one device tree. Keyed by a 64-bit hash of the bytes, always CONFIRMED by a full byte comparison (a hash collision cannot
// give a wrong answer). LRU of at most 4 entries and 512 MiB (estimated as 6x the packed size); a larger mesh is used once and not kept. Per-entry builds run once (std::call_once).
struct MeshEntry {
    string bytes;
    int code = meshvol::kOk;           // result of parse + closed check, cached too (an invalid mesh is invalid every time)
    meshvol::Packed m;
    meshcontains::Bvh bvh; std::once_flag bvhOnce;
#ifdef MESHVOL_GPU
    gpupip::Tree tree; std::once_flag treeOnce;
#endif
    const meshcontains::Bvh& cpuBvh() { std::call_once(bvhOnce, [&] { bvh.build(m.v, m.t); }); return bvh; }
};
std::atomic<long> g_meshCacheHits{0}, g_meshCacheMisses{0};
std::mutex g_meshCacheMutex;
std::vector<std::shared_ptr<MeshEntry>> g_meshCache;   // most recently used last
std::shared_ptr<MeshEntry> meshLookup(const uint8_t* p, size_t len, bool useCache) {
    if (useCache) {
        std::lock_guard<std::mutex> g(g_meshCacheMutex);
        for (size_t i = 0; i < g_meshCache.size(); i++) {
            auto& e = g_meshCache[i];
#if defined(MESHVOL_CACHEMUT) && MESHVOL_CACHEMUT == 1
            if (e->bytes.size() == len) {   // mutant: key is the length only, no byte comparison
#else
            if (e->bytes.size() == len && std::memcmp(e->bytes.data(), p, len) == 0) {
#endif
                auto hit = e; g_meshCache.erase(g_meshCache.begin() + i); g_meshCache.push_back(hit); g_meshCacheHits++; return hit;
            }
        }
    }
    auto e = std::make_shared<MeshEntry>();
    e->bytes.assign(reinterpret_cast<const char*>(p), len);
    if ((e->code = meshvol::parsePacked(p, len, &e->m)) == meshvol::kOk) e->code = meshvol::checkClosed(e->m);
    g_meshCacheMisses++;
    if (useCache && len * 6 <= (size_t)512 << 20) {
        std::lock_guard<std::mutex> g(g_meshCacheMutex);
        g_meshCache.push_back(e);
        size_t total = 0; for (auto& x : g_meshCache) total += x->bytes.size() * 6;
        while (g_meshCache.size() > 4 || (g_meshCache.size() > 1 && total > ((size_t)512 << 20))) { total -= g_meshCache.front()->bytes.size() * 6; g_meshCache.erase(g_meshCache.begin()); }
    }
    return e;
}

// Returns false if cancelled (deadline or kill, polled every 256 points) before all points were done.
bool containsCpu(MeshEntry& me, const std::vector<double>& pts, string* verdicts, bool (*cancel)(void*), void* cancelArg) {
    const meshvol::Packed& m = me.m;
    const size_t n = pts.size() / 3;
    verdicts->resize(n);
    const bool useBvh = n >= 8;                     // a tree pays for itself only with several points; the verdicts are identical either way
    const meshcontains::Bvh* bvh = useBvh ? &me.cpuBvh() : nullptr;
    std::vector<uint32_t> scratch;
    for (size_t i = 0; i < n; i++) {
        if ((i & 255) == 255 && cancel(cancelArg)) return false;
        const auto v = useBvh ? meshcontains::classifyBvh(m.v, m.t, *bvh, &pts[3 * i], &scratch) : meshcontains::classify(m.v, m.t, &pts[3 * i]);
        (*verdicts)[i] = v == meshcontains::kIn ? 'I' : v == meshcontains::kOn ? 'B' : 'O';
    }
    return true;
}
bool containsDoc(const Spec& spec, const uint8_t* d, size_t len, DocOut& o, bool (*cancel)(void*), void* cancelArg) {
    if (!bm::validateDoc(d, len)) { o.code = meshvol::kBadDocument; return true; }
    bm::Elem e;
    if (!bm::findField(d, len, spec.input, &e)) { o.code = meshvol::kMissingField; return true; }
    if (e.type != bm::kBinData) { o.code = meshvol::kNotBinData; return true; }
    if (e.val[4] != 0) { o.code = meshvol::kBadSubtype; return true; }
    auto me = meshLookup(e.val + 5, e.vlen - 5, spec.meshCache);
    if ((o.code = me->code) != meshvol::kOk) return true;
    bm::Elem pe;
    if (!bm::findField(d, len, spec.points, &pe)) { o.code = meshvol::kMissingField; return true; }
    std::vector<double> pts;
    if ((o.code = readPoints(pe, &pts)) != meshvol::kOk) return true;
    if (gpuEligible(spec, pts.size() / 3)) {
        const int r = containsGpu(*me, pts, spec, &o.verdicts, cancel, cancelArg);
        if (r == 1) return true;
        if (r == 2) return false;
    }
    return containsCpu(*me, pts, &o.verdicts, cancel, cancelArg);
}

// Returns false if cancelled (deadline or kill) before every document was done.
bool computeBatch(const Spec& spec, const DocIn* in, size_t n, DocOut* out, CancelFn cancel, void* cancelArg) {
    g_computeCalls++;
    if ((long)n > g_maxBatch) g_maxBatch = (long)n;
    if (spec.plant == "throw") throw std::runtime_error("planted C++ exception inside computeBatch");
    if (spec.plant == "segv") { volatile int* p = nullptr; *p = 42; }
    if (spec.plant == "spinPolite") {  // well-behaved: polls the cancel hook (deadline + interrupt) every iteration
        const int64_t until = nowMs() + spec.spinMs;
        while (nowMs() < until) { if (cancel(cancelArg)) return false; }
    }
    if (spec.plant == "spinRude") {    // ill-behaved: never polls; the host can neither stop it nor see the deadline pass until it returns
        const int64_t until = nowMs() + spec.spinMs;
        while (nowMs() < until) {}
    }
    if (!spec.probe.empty()) {
        string d = spec.probe == "config" ? configDoc() : spec.probe == "vulkan" ? vulkanProbe() : spec.probe == "symbol" ? (bm::Builder().i32("collideMe", collide_me()).finish()) : countersDoc();
        for (size_t i = 0; i < n; i++) out[i].probeDoc = d;
        return true;
    }
    for (size_t i = 0; i < n; i++) {
        if ((i & 15) == 0 && cancel(cancelArg)) return false;
        DocOut& o = out[i];
        if (spec.contains) { if (!containsDoc(spec, in[i].data, in[i].len, o, cancel, cancelArg)) return false; continue; }
        if (!bm::validateDoc(in[i].data, in[i].len)) { o.code = meshvol::kBadDocument; continue; }
        bm::Elem e;
        if (!bm::findField(in[i].data, in[i].len, spec.input, &e)) { o.code = meshvol::kMissingField; continue; }
        if (e.type != bm::kBinData) { o.code = meshvol::kNotBinData; continue; }
        if (e.val[4] != 0) { o.code = meshvol::kBadSubtype; continue; }
        o.r = meshvol::volumeOfPacked(e.val + 5, e.vlen - 5);
        o.code = o.r.code;
    }
    return true;
}

string outputDoc(const Spec& spec, const DocIn& in, const DocOut& o) {
    bm::Builder out;
    if (bm::validateDoc(in.data, in.len)) {  // copy every field except one named like the output
        const uint8_t* p = in.data + 4;
        const uint8_t* end = in.data + in.len - 1;
        while (p < end) {
            bm::Elem e;
            bm::readElem(p, end, &e);
            p = e.end;
            if (e.name != spec.output) out.copyElem(e);
        }
    }
    string sub;
    if (!o.probeDoc.empty()) sub = o.probeDoc;
    else if (o.code != meshvol::kOk) {
        bm::Builder er;
        er.i32("code", o.code).str("reason", meshvol::errName(o.code));
        bm::Builder w;
        w.doc("error", er.finish());
        sub = w.finish();
    } else if (spec.contains) {
        out.str(spec.output, o.verdicts);   // a string of 'O' / 'I' / 'B', one per point
        return out.finish();
    } else {
        bm::Builder w;
        w.str("exact", o.r.exact).dbl("approx", o.r.approx);
        sub = w.finish();
    }
    out.doc(spec.output, sub);
    return out.finish();
}

// ------------------------------------------------------------------ MP14c: configuration (extensionOptions)
// The host serialises the YAML map `extensionOptions` of the extension's .conf with YAML::Dump and hands the text to initialize() through the portal.
// Only a flat `key: value` map is accepted; anything else fails initialize (a wrong config must not start silently).
struct Config {
#if defined(MESHVOL_MUTANT_CFG) && MESHVOL_MUTANT_CFG == 2
    bool gpu = true, gpuRequired = false;  // mutant: default flipped to on
#else
    bool gpu = false, gpuRequired = false;  // gpu is OFF by default (owner requirement)
#endif
    bool gpuProbed = false, gpuReady = false;
    string gpuReason = "not requested";
};
Config g_cfg;
std::atomic<long> g_vulkanProbes{0}, g_gpuBatches{0}, g_gpuFallbacks{0}, g_gpuShadowMismatch{0}, g_gpuUncertain{0}, g_gpuPoints{0};
string vulkanProbe();

string trimWs(const string& x) {
    size_t a = x.find_first_not_of(" \t\r"), b = x.find_last_not_of(" \t\r");
    return a == string::npos ? string() : x.substr(a, b - a + 1);
}
bool parseBoolStrict(const string& key, const string& v) {
    if (v == "true") return true;
    if (v == "false") return false;
    throw ExtErr{31, "meshvolume: option '" + key + "' must be true or false, got '" + v + "'"};
}
Config parseOptions(const string& textIn) {
    // Real YAML::Dump output (fork yaml-cpp, see docs/logs/mp14c_yaml_dump_probe.out): block map with no indentation and no trailing newline
    // ("gpu: true\ngpuRequired: true"), "{}" for an empty map, "~" for a null map, a single-line flow map "{gpu: true, gpuRequired: false}" when the
    // conf used flow style, duplicate keys kept, quotes dropped ("true" and true are indistinguishable), "yes"/"True"/"1" kept verbatim.
    Config c;
    string text = trimWs(textIn);
    std::vector<string> entries;
    if (text.empty() || text == "{}" || text == "~") {
    } else if (text[0] == '{') {
        if (text.back() != '}' || text.find_first_of("[]{", 1) != string::npos || text.find('\n') != string::npos)
            throw ExtErr{32, "meshvolume: only a flat map of options is supported: '" + text + "'"};
        string inner = text.substr(1, text.size() - 2);
        size_t p0 = 0;
        while (p0 <= inner.size()) {
            size_t cm = inner.find(',', p0);
            entries.push_back(trimWs(inner.substr(p0, cm == string::npos ? string::npos : cm - p0)));
            p0 = cm == string::npos ? inner.size() + 1 : cm + 1;
        }
    } else {
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t nl = text.find('\n', pos);
            string raw = text.substr(pos, nl == string::npos ? string::npos : nl - pos);
            pos = nl == string::npos ? text.size() + 1 : nl + 1;
            if (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t')) throw ExtErr{32, "meshvolume: nested or indented option lines are not supported: '" + trimWs(raw) + "'"};
            string line = trimWs(raw);
            if (line.empty() || line[0] == '#' || line == "---") continue;
            entries.push_back(line);
        }
    }
    std::vector<string> seen;
    for (const string& line : entries) {
        size_t colon = line.find(':');
        if (colon == string::npos) throw ExtErr{33, "meshvolume: option is not 'key: value': '" + line + "'"};
        string key = trimWs(line.substr(0, colon)), val = trimWs(line.substr(colon + 1));
#if !(defined(MESHVOL_MUTANT_CFG) && MESHVOL_MUTANT_CFG == 5)
        for (const string& k : seen) if (k == key) throw ExtErr{37, "meshvolume: duplicate option '" + key + "'"};
#endif
        seen.push_back(key);
        if (key == "gpu") c.gpu = parseBoolStrict(key, val);
        else if (key == "gpuRequired") c.gpuRequired = parseBoolStrict(key, val);
#if defined(MESHVOL_MUTANT_CFG) && MESHVOL_MUTANT_CFG == 4
        else continue;  // mutant: strictness removed
#else
        else throw ExtErr{34, "meshvolume: unknown extension option '" + key + "' (known: gpu, gpuRequired)"};
#endif
    }
    if (c.gpuRequired && !c.gpu) throw ExtErr{35, "meshvolume: gpuRequired is true but gpu is not true"};
    return c;
}
// Reads an int64 field out of a probe document (0 when absent).
int64_t probeInt(const string& doc, const char* name) {
    bm::Elem e;
    if (!bm::findField(reinterpret_cast<const uint8_t*>(doc.data()), doc.size(), name, &e) || e.type != bm::kInt64) return 0;
    int64_t v; std::memcpy(&v, e.val, 8); return v;
}
bool probeBool(const string& doc, const char* name) {
    bm::Elem e;
    return bm::findField(reinterpret_cast<const uint8_t*>(doc.data()), doc.size(), name, &e) && e.type == bm::kBool && e.val[0];
}
string probeStr(const string& doc, const char* name) {
    bm::Elem e;
    if (!bm::findField(reinterpret_cast<const uint8_t*>(doc.data()), doc.size(), name, &e) || e.type != bm::kString) return string();
    return string(reinterpret_cast<const char*>(e.val + 4), e.vlen - 5);
}
// Runs the Vulkan probe in a helper thread and waits at most `limitMs`; a probe that hangs in a driver is abandoned (detached), never joined.
// A probe that CRASHES inside a driver takes the host process down (MP14a bar 5): not survivable in-process.
bool probeWithDeadline(string* docOut, int64_t limitMs) {
    struct Shared { std::mutex m; std::condition_variable cv; bool done = false; string doc; };
    auto sh = std::make_shared<Shared>();
    std::thread([sh] {
        const char* hang = std::getenv("MESHVOL_PLANT_PROBE_HANG_MS");  // test-only plant
        if (hang) std::this_thread::sleep_for(std::chrono::milliseconds(std::atol(hang)));
        string d;
        try { d = vulkanProbe(); } catch (...) { d.clear(); }  // an exception must never escape a detached thread (std::terminate would kill the host)
        std::lock_guard<std::mutex> g(sh->m);
        sh->doc = std::move(d);
        sh->done = true;
        sh->cv.notify_all();
    }).detach();
    std::unique_lock<std::mutex> lk(sh->m);
    if (!sh->cv.wait_for(lk, std::chrono::milliseconds(limitMs), [&] { return sh->done; })) return false;
    *docOut = sh->doc;
    return true;
}
void logMsg(const string& msg) {
    if (!g_hostServices || !g_hostServices->vtable->get_logger) return;
    MongoExtensionLogger* lg = g_hostServices->vtable->get_logger();
    if (!lg) return;
    MongoExtensionLogMessage m{};
    m.code = 7001400;
    m.message = {reinterpret_cast<const uint8_t*>(msg.data()), msg.size()};
    m.type = kLog;
    m.attributes = {0, nullptr};
    m.severityOrLevel.severity = kInfo;
    MongoExtensionStatus* st = lg->vtable->log(&m);
    consume(st);
}
void logGateLine(const Config& c) {
    logMsg(string("meshvolume: gpu requested=") + (c.gpu ? "true" : "false") + " ready=" + (c.gpuReady ? "true" : "false") + " engine=" + (c.gpu && c.gpuReady ? "gpu" : "cpu") + " reason=" + c.gpuReason);
}
#if defined(MESHVOL_MUTANT_CFG) && MESHVOL_MUTANT_CFG == 7
// mutant: opens the loader from a static initialiser, before the config is parsed (the probe counter stays 0)
struct EarlyOpen { EarlyOpen() { pc::openLib("libvulkan.1.dylib"); pc::openLib("/opt/homebrew/lib/libvulkan.1.dylib"); pc::openLib("libvulkan.so.1"); pc::openLib("vulkan-1.dll"); } } g_earlyOpen;
#endif
// Applies the gate: only when gpu is true does the extension touch Vulkan (one probe, at initialize, cached for the process).
void applyGpuGate(Config& c) {
    if (!c.gpu) { c.gpuReason = "not requested"; logGateLine(c); return; }
    c.gpuProbed = true;
    ++g_vulkanProbes;
    string doc;
    const long limit = std::getenv("MESHVOL_PROBE_LIMIT_MS") ? std::atol(std::getenv("MESHVOL_PROBE_LIMIT_MS")) : 10000;
    if (!probeWithDeadline(&doc, limit)) {
        c.gpuReady = false;
        c.gpuReason = "vulkan probe timed out";
    } else if (doc.empty()) {
        c.gpuReady = false;
        c.gpuReason = "vulkan probe failed";
    } else if (!probeBool(doc, "ok")) {
        c.gpuReady = false;
        c.gpuReason = probeBool(doc, "loaderLoaded") ? "vulkan instance creation failed" : "vulkan loader not found";
#ifdef MESHVOL_VULKAN_PROBE
#else
        c.gpuReason = "built without vulkan support";
#endif
    } else if (probeInt(doc, "deviceCount") == 0) {
        c.gpuReady = false;
        c.gpuReason = "no physical devices";
    } else if (probeInt(doc, "hardwareDeviceCount") == 0) {
        c.gpuReady = false;
        c.gpuReason = "software device only";
    } else {
        c.gpuReady = true;
        c.gpuReason = "hardware device present";
    }
#if defined(MESHVOL_MUTANT_CFG) && MESHVOL_MUTANT_CFG == 6
    if (!c.gpuReady && c.gpuReason != "software device only") c.gpuReady = true;  // mutant: probe error swallowed
#endif
    logGateLine(c);
    if (!c.gpuReady && c.gpuRequired) throw ExtErr{36, "meshvolume: gpuRequired is true but the GPU is not ready: " + c.gpuReason};
}
string g_gpuLastFailure;   // why the device path was disabled (written under the engine mutex, read by the config probe)
std::atomic<bool> g_gpuDisabled{false};   // the breaker: one device failure, timeout or shadow mismatch disables the device path until restart (a failed submission poisons the context)
const char* engineName() {
#ifdef MESHVOL_GPU
    return g_cfg.gpu && g_cfg.gpuReady && !g_gpuDisabled.load() ? "gpu" : "cpu";   // "gpu" = the device path is available for large documents; small ones and engine:\"cpu\" stay on the CPU
#else
    return "cpu";
#endif
}
string configDoc() {
    bm::Builder b;
    b.boolean("gpuRequested", g_cfg.gpu).boolean("gpuRequired", g_cfg.gpuRequired).boolean("gpuProbed", g_cfg.gpuProbed)
        .boolean("gpuReady", g_cfg.gpuReady).str("engine", engineName()).str("reason", g_cfg.gpuReason).i64("vulkanProbes", g_vulkanProbes)
        .i64("gpuBatches", g_gpuBatches).i64("gpuFallbacks", g_gpuFallbacks).i64("gpuShadowMismatch", g_gpuShadowMismatch).i64("gpuUncertain", g_gpuUncertain).i64("gpuPoints", g_gpuPoints).str("gpuLastFailure", g_gpuLastFailure).i64("meshCacheHits", g_meshCacheHits).i64("meshCacheMisses", g_meshCacheMisses);
    return b.finish();
}

// ------------------------------------------------------------------ MP14d: the device path (only reachable when the MP14c gate says gpu requested and ready)
void logMsg(const string& msg);
bool gpuEligible(const Spec& spec, size_t nPoints) {
#ifdef MESHVOL_GPU
#if defined(MESHVOL_GPUMUT) && MESHVOL_GPUMUT == 3
    return spec.contains && spec.engine == "auto" && g_cfg.gpu && g_cfg.gpuReady && !g_gpuDisabled.load();   // mutant: threshold ignored
#else
    return spec.contains && spec.engine == "auto" && g_cfg.gpu && g_cfg.gpuReady && !g_gpuDisabled.load() && nPoints >= spec.gpuMinPoints;
#endif
#else
    (void)spec; (void)nPoints; return false;
#endif
}
int containsGpu(MeshEntry& me, const std::vector<double>& pts, const Spec& spec, string* verdicts, bool (*cancel)(void*), void* cancelArg) {
#ifdef MESHVOL_GPU
    const meshvol::Packed& m = me.m;
    const size_t n = pts.size() / 3;
    #if defined(MESHVOL_GPUMUT) && MESHVOL_GPUMUT == 2
    auto fallback = [&](const string& why) { g_gpuFallbacks++; (void)why; return 0; };   // mutant: breaker never opens
#else
    auto fallback = [&](const string& why) { g_gpuFallbacks++; g_gpuLastFailure = why; g_gpuDisabled = true; logMsg("meshvolume: device path disabled, falling back to the CPU path: " + why); return 0; };
#endif
    if (spec.plant == "gpuFail") return fallback("planted device failure");
    gpupip::Engine& E = gpupip::engine();
    std::lock_guard<std::mutex> lk(E.mutex);
    if (g_gpuDisabled.load()) return 0;
    if (cancel(cancelArg)) return 2;
    std::call_once(me.treeOnce, [&] { me.tree = gpupip::buildTree(m.v, m.t); });
    const gpupip::Tree& tree = me.tree;
    std::vector<uint32_t> dv; string err;
    if (!E.classify(m.v, m.t, tree, pts, &dv, 10'000'000'000ull, &err, spec.plant)) return fallback(err);
    if (spec.plant == "gpuWrong") for (size_t i = 0; i < n; i += 64) if (dv[i] != 2) dv[i] ^= 1;   // planted: wrong definite verdicts where the shadow check looks
    const meshcontains::Bvh& bvh = me.cpuBvh();
    std::vector<uint32_t> scratch;
    verdicts->assign(n, 'O');
    long uncertain = 0;
    for (size_t i = 0; i < n; i++) {
        if ((i & 255) == 255 && cancel(cancelArg)) return 2;
#if defined(MESHVOL_GPUMUT) && MESHVOL_GPUMUT == 4
        if (dv[i] == 2) { (*verdicts)[i] = 'O'; continue; }   // mutant: UNCERTAIN trusted as OUT
#endif
        if (dv[i] == 2) {   // the device is not sure: the exact CPU path decides
            const auto v = meshcontains::classifyBvh(m.v, m.t, bvh, &pts[3 * i], &scratch);
            (*verdicts)[i] = v == meshcontains::kIn ? 'I' : v == meshcontains::kOn ? 'B' : 'O'; uncertain++;
        } else (*verdicts)[i] = dv[i] ? 'I' : 'O';
    }
    // shadow check: every 64th point with a definite device verdict is recomputed exactly; one mismatch disables the device path and the whole batch is redone on the CPU
#if defined(MESHVOL_GPUMUT) && MESHVOL_GPUMUT == 1
    for (size_t i = 0; i < 0; i += 64) {   // mutant: shadow check removed
#else
    for (size_t i = 0; i < n; i += 64) {
#endif
        if (dv[i] == 2) continue;
        const auto v = meshcontains::classifyBvh(m.v, m.t, bvh, &pts[3 * i], &scratch);
        const char want = v == meshcontains::kIn ? 'I' : v == meshcontains::kOn ? 'B' : 'O';
        if ((*verdicts)[i] != want) { g_gpuShadowMismatch++; return fallback("shadow check mismatch at point " + std::to_string(i)); }
    }
    g_gpuBatches++; g_gpuPoints += (long)n; g_gpuUncertain += uncertain;
    return 1;
#else
    (void)me; (void)pts; (void)spec; (void)verdicts; (void)cancel; (void)cancelArg; return 0;
#endif
}

// ------------------------------------------------------------------ probes
string countersDoc() {
    bm::Builder b;
    b.i64("computeCalls", g_computeCalls).i64("sourcePulls", g_sourcePulls).i64("getNextCalls", g_getNextCalls).i64("throwsCaught", g_throwsCaught)
        .i64("maxBatch", g_maxBatch).i64("liveBufs", g_liveBufs).i64("liveStatuses", g_liveStatuses).i64("liveObjs", g_liveObjs).i64("docsOut", g_docsOut);
    return b.finish();
}

string vulkanProbe() {
    bm::Builder b;
#ifdef MESHVOL_VULKAN_PROBE
    const int64_t t0 = nowMs();
    pc::Lib h = nullptr;
#ifdef _WIN32
    const char* cands[] = {"vulkan-1.dll"};
#else
    const char* cands[] = {"libvulkan.1.dylib", "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib", "libvulkan.so.1"};
#endif
    const char* used = "";
    for (const char* c : cands) if ((h = pc::openLib(c))) { used = c; break; }
    b.boolean("loaderLoaded", h != nullptr).str("loader", used);
    if (!h) { const std::string de = pc::libError(); b.boolean("ok", false).str("detail", de); return b.finish(); }
    auto createInstance = (PFN_vkCreateInstance)pc::sym(h, "vkCreateInstance");
    auto destroyInstance = (PFN_vkDestroyInstance)pc::sym(h, "vkDestroyInstance");
    auto enumExt = (PFN_vkEnumerateInstanceExtensionProperties)pc::sym(h, "vkEnumerateInstanceExtensionProperties");
    if (!createInstance || !destroyInstance || !enumExt) { b.boolean("ok", false).str("detail", "loader symbols missing"); return b.finish(); }
    uint32_t n = 0;
    enumExt(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    enumExt(nullptr, &n, exts.data());
    bool portability = false;
    for (auto& e : exts) if (!strcmp(e.extensionName, "VK_KHR_portability_enumeration")) portability = true;
    const char* ie[] = {"VK_KHR_portability_enumeration"};
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "mp14a-extension-probe";
    ai.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    if (portability) { ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR; ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = ie; }
    VkInstance inst = nullptr;
    const VkResult r = createInstance(&ci, nullptr, &inst);
    b.i32("vkResult", (int32_t)r).boolean("portabilityEnumeration", portability);
    int64_t devCount = 0, hwCount = 0;
    if (r == VK_SUCCESS) {
        auto enumDev = (PFN_vkEnumeratePhysicalDevices)pc::sym(h, "vkEnumeratePhysicalDevices");
        auto getProps = (PFN_vkGetPhysicalDeviceProperties)pc::sym(h, "vkGetPhysicalDeviceProperties");
        uint32_t nd = 0;
        if (enumDev && getProps && enumDev(inst, &nd, nullptr) == VK_SUCCESS && nd > 0) {
            std::vector<VkPhysicalDevice> devs(nd);
            if (enumDev(inst, &nd, devs.data()) == VK_SUCCESS) {
                devCount = nd;
                for (uint32_t i = 0; i < nd; i++) {
                    VkPhysicalDeviceProperties pr{};
                    getProps(devs[i], &pr);
                    if (pr.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) hwCount++;  // lavapipe / llvmpipe report TYPE_CPU
                }
            }
        }
        // The probe's VkInstance is deliberately NEVER destroyed: when the last instance goes the loader dlcloses the ICD (e.g. Mesa Dozen's libvulkan_dzn.so), and a driver thread that is still
        // starting then executes unmapped code (SIGSEGV in a libc-started thread at server startup, found in the real mongod on the 4060 Ti, 2026-11-16). One idle instance for the life of the
        // process keeps the ICD mapped. (destroyInstance stays resolved so a missing symbol is still reported.)
        (void)destroyInstance;
    }
    b.i64("deviceCount", devCount).i64("hardwareDeviceCount", hwCount);
    b.boolean("ok", r == VK_SUCCESS).i64("elapsedMs", nowMs() - t0);
    // the loader is intentionally left loaded (dlclose of loaders/ICDs inside a host process is its own hazard)
#else
    b.boolean("ok", false).str("detail", "built without MESHVOL_VULKAN_PROBE");
#endif
    return b.finish();
}

// ------------------------------------------------------------------ exec stage
struct Cancel {
    const MongoExtensionQueryExecutionContext* ctx;
    int64_t deadlineMs;
    string reason;
    int32_t code = 0;
    int calls = 0;
};
bool cancelHook(void* a) {
    auto* c = static_cast<Cancel*>(a);
    if ((c->calls++ & 7) != 0) return false;  // cheap poll: the clock every 8th call...
    if (nowMs() >= c->deadlineMs) { c->code = 50; c->reason = "$meshVolume: operation exceeded the deadline reported by the host (cooperative stop)"; return true; }
    // ...and the host's kill state through check_for_interrupt
    auto* st = new ErrStatus(0, "");
    MongoExtensionStatus* r = c->ctx->vtable->check_for_interrupt(c->ctx, st);
    const int32_t code = st->code;
    const string reason = st->reason;
    st->vtable->destroy(st);
    if (r) {
        const int32_t rc = r->vtable->get_code(r);
        r->vtable->destroy(r);
        (void)rc;
    }
    if (code != 0) { c->code = code; c->reason = reason; return true; }
    return false;
}

struct ExecStage : MongoExtensionExecAggStage {
    Spec spec;
    MongoExtensionExecAggStage* source = nullptr;
    std::deque<string> queue;
    bool sourceEof = false;
    static const MongoExtensionExecAggStageVTable vt;
    explicit ExecStage(Spec s) : MongoExtensionExecAggStage{&vt}, spec(std::move(s)) { g_liveObjs++; }
    ~ExecStage() { g_liveObjs--; }

    void fill(MongoExtensionQueryExecutionContext* ctx) {
        int64_t deadline = INT64_MAX;
        consume(ctx->vtable->get_deadline_timestamp_ms(ctx, &deadline));
        std::vector<string> docs;
        docs.reserve(std::min<size_t>(spec.batchSize, 1 << 16));
        Cancel cancel{ctx, deadline, "", 0, 0};
        while (docs.size() < spec.batchSize) {
            if ((docs.size() & 255) == 255 && nowMs() >= deadline) throw ExtErr{50, "$meshVolume: past the deadline while gathering the batch"};
            if (!source) throw ExtErr{19, "$meshVolume: no source stage set"};
            MongoExtensionGetNextResult r{};
            r.resultDocument.type = kByteView;
            r.resultDocument.bytes.view = {nullptr, 0};
            r.resultMetadata.type = kByteView;
            r.resultMetadata.bytes.view = {nullptr, 0};
            g_sourcePulls++;
            consume(source->vtable->get_next(source, ctx, &r));
            if (r.code == kEOF) { sourceEof = true; break; }
            if (r.code == kPauseExecution) break;
            MongoExtensionByteView v;
            if (r.resultDocument.type == kByteBuf) {
                v = r.resultDocument.bytes.buf->vtable->get_view(r.resultDocument.bytes.buf);
                docs.emplace_back(reinterpret_cast<const char*>(v.data), v.len);
                r.resultDocument.bytes.buf->vtable->destroy(r.resultDocument.bytes.buf);
            } else {
                v = r.resultDocument.bytes.view;  // valid only until the next source get_next(): copy now
                docs.emplace_back(reinterpret_cast<const char*>(v.data), v.len);
            }
            if (r.resultMetadata.type == kByteBuf && r.resultMetadata.bytes.buf) r.resultMetadata.bytes.buf->vtable->destroy(r.resultMetadata.bytes.buf);
        }
        if (docs.empty()) return;
        std::vector<DocIn> in(docs.size());
        for (size_t i = 0; i < docs.size(); i++) in[i] = {reinterpret_cast<const uint8_t*>(docs[i].data()), docs[i].size()};
        std::vector<DocOut> out(docs.size());
        if (!computeBatch(spec, in.data(), in.size(), out.data(), cancelHook, &cancel)) throw ExtErr{cancel.code ? cancel.code : 50, cancel.reason};
        for (size_t i = 0; i < docs.size(); i++) queue.push_back(outputDoc(spec, in[i], out[i]));
    }
};

const MongoExtensionExecAggStageVTable ExecStage::vt = {
    /*destroy*/ [](MongoExtensionExecAggStage* s) { delete static_cast<ExecStage*>(s); },
    /*get_next*/
    [](MongoExtensionExecAggStage* s, MongoExtensionQueryExecutionContext* ctx, MongoExtensionGetNextResult* res) -> MongoExtensionStatus* {
        return guard([&] {
            auto* e = static_cast<ExecStage*>(s);
            g_getNextCalls++;
            if (e->spec.plant == "throwGetNext") throw std::runtime_error("planted C++ exception in get_next");
            if (e->queue.empty() && !e->sourceEof) e->fill(ctx);
            res->resultMetadata.type = kByteView;
            res->resultMetadata.bytes.view = {nullptr, 0};
            if (e->queue.empty()) {
                res->code = e->sourceEof ? kEOF : kPauseExecution;
                res->resultDocument.type = kByteView;
                res->resultDocument.bytes.view = {nullptr, 0};
                return;
            }
            auto* buf = new BufImpl(std::move(e->queue.front()));
            e->queue.pop_front();
            g_docsOut++;
            res->code = kAdvanced;
            res->resultDocument.type = kByteBuf;  // ownership passes to the host, which must destroy it
            res->resultDocument.bytes.buf = buf;
        });
    },
    /*get_name*/ [](const MongoExtensionExecAggStage* s) -> MongoExtensionByteView { const char* n = static_cast<const ExecStage*>(s)->spec.stageName(); return {reinterpret_cast<const uint8_t*>(n), std::strlen(n)}; },
    /*create_metrics*/ [](const MongoExtensionExecAggStage*, MongoExtensionOperationMetrics** m) -> MongoExtensionStatus* { *m = nullptr; return &g_ok; },
    /*set_source*/
    [](MongoExtensionExecAggStage* s, MongoExtensionExecAggStage* src) -> MongoExtensionStatus* {
        return guard([&] { static_cast<ExecStage*>(s)->source = src; });
    },
    /*open*/
    [](MongoExtensionExecAggStage* s) -> MongoExtensionStatus* {
        return guard([&] {
            auto* e = static_cast<ExecStage*>(s);
            if (e->spec.plant == "throwOpen") throw std::runtime_error("planted C++ exception in open");
            e->queue.clear();
            e->sourceEof = false;
        });
    },
    /*reopen*/
    [](MongoExtensionExecAggStage* s) -> MongoExtensionStatus* {
        return guard([&] { auto* e = static_cast<ExecStage*>(s); e->queue.clear(); e->sourceEof = false; });
    },
    /*close*/ [](MongoExtensionExecAggStage* s) -> MongoExtensionStatus* { return guard([&] { static_cast<ExecStage*>(s)->queue.clear(); }); },
    /*explain*/
    [](const MongoExtensionExecAggStage*, MongoExtensionQueryExecutionContext*, MongoExtensionExplainVerbosity, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] {
            bm::Builder b;
            b.i64("computeCalls", g_computeCalls);
            *out = new BufImpl(b.finish());
        });
    },
};

// ------------------------------------------------------------------ logical stage, AST node, parse node, descriptor
struct LogicalStage : MongoExtensionLogicalAggStage {
    Spec spec;
    static const MongoExtensionLogicalAggStageVTable vt;
    explicit LogicalStage(Spec s) : MongoExtensionLogicalAggStage{&vt}, spec(std::move(s)) { g_liveObjs++; }
    ~LogicalStage() { g_liveObjs--; }
};
const MongoExtensionLogicalAggStageVTable LogicalStage::vt = {
    /*destroy*/ [](MongoExtensionLogicalAggStage* s) { delete static_cast<LogicalStage*>(s); },
    /*get_name*/ [](const MongoExtensionLogicalAggStage* s) -> MongoExtensionByteView { const char* n = static_cast<const LogicalStage*>(s)->spec.stageName(); return {reinterpret_cast<const uint8_t*>(n), std::strlen(n)}; },
    /*serialize*/
    [](const MongoExtensionLogicalAggStage* s, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new BufImpl(static_cast<const LogicalStage*>(s)->spec.toStageBson()); });
    },
    /*explain*/
    [](const MongoExtensionLogicalAggStage* s, MongoExtensionQueryExecutionContext*, MongoExtensionExplainVerbosity, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] {
            bm::Builder inner;
            inner.i32("batchSize", (int32_t)static_cast<const LogicalStage*>(s)->spec.batchSize);
            if (static_cast<const LogicalStage*>(s)->spec.contains) inner.str("engine", "cpu");  // MP14d: no GPU kernel yet
            bm::Builder b;
            b.doc(static_cast<const LogicalStage*>(s)->spec.stageName(), inner.finish());
            *out = new BufImpl(b.finish());
        });
    },
    /*compile*/
    [](const MongoExtensionLogicalAggStage* s, MongoExtensionExecAggStage** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new ExecStage(static_cast<const LogicalStage*>(s)->spec); });
    },
    /*get_distributed_plan_logic*/ [](const MongoExtensionLogicalAggStage*, MongoExtensionDistributedPlanLogic** out) -> MongoExtensionStatus* { *out = nullptr; return &g_ok; },
    /*clone*/
    [](const MongoExtensionLogicalAggStage* s, MongoExtensionLogicalAggStage** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new LogicalStage(static_cast<const LogicalStage*>(s)->spec); });
    },
    /*set_vector_search_limit_for_optimization_deprecated*/ [](MongoExtensionLogicalAggStage*, long long*) -> MongoExtensionStatus* { return &g_ok; },
    /*evaluate_pipeline_rewrite_rule_precondition*/
    [](const MongoExtensionLogicalAggStage*, MongoExtensionByteView, const MongoExtensionPipelineRewriteContext*, bool* r) -> MongoExtensionStatus* { *r = false; return &g_ok; },
    /*evaluate_pipeline_rewrite_rule_transform*/
    [](MongoExtensionLogicalAggStage*, MongoExtensionByteView, MongoExtensionPipelineRewriteContext*, bool* r) -> MongoExtensionStatus* { *r = false; return &g_ok; },
    /*get_filter*/ [](const MongoExtensionLogicalAggStage*, MongoExtensionByteBuf** out) -> MongoExtensionStatus* { *out = nullptr; return &g_ok; },
    /*apply_pipeline_suffix_dependencies*/ [](MongoExtensionLogicalAggStage*, const MongoExtensionPipelineDependencies*) -> MongoExtensionStatus* { return &g_ok; },
    /*get_sort_pattern*/ [](const MongoExtensionLogicalAggStage*, MongoExtensionByteBuf** out) -> MongoExtensionStatus* { *out = nullptr; return &g_ok; },
    /*skip_stream*/ [](MongoExtensionLogicalAggStage*, MongoExtensionStreamType) -> MongoExtensionStatus* { return &g_ok; },
    /*get_docs_needed_bounds*/ [](const MongoExtensionLogicalAggStage*, MongoExtensionByteBuf** out) -> MongoExtensionStatus* { *out = nullptr; return &g_ok; },
};

struct AstNode : MongoExtensionAggStageAstNode {
    Spec spec;
    static const MongoExtensionAggStageAstNodeVTable vt;
    explicit AstNode(Spec s) : MongoExtensionAggStageAstNode{&vt}, spec(std::move(s)) { g_liveObjs++; }
    ~AstNode() { g_liveObjs--; }
};
const MongoExtensionAggStageAstNodeVTable AstNode::vt = {
    /*destroy*/ [](MongoExtensionAggStageAstNode* n) { delete static_cast<AstNode*>(n); },
    /*get_name*/ [](const MongoExtensionAggStageAstNode* s) -> MongoExtensionByteView { const char* n = static_cast<const AstNode*>(s)->spec.stageName(); return {reinterpret_cast<const uint8_t*>(n), std::strlen(n)}; },
    /*get_properties*/
    [](const MongoExtensionAggStageAstNode*, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] {
            bm::Builder b;  // MongoExtensionStaticProperties (public/extension_agg_stage_static_properties.idl), non-default fields only
            b.str("streamType", "streaming").boolean("requiresInputDocSource", true).boolean("preservesUpstreamMetadata", true);
            *out = new BufImpl(b.finish());
        });
    },
    /*promote*/
    [](const MongoExtensionAggStageAstNode* n, const MongoExtensionCatalogContext*, MongoExtensionLogicalAggStage** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new LogicalStage(static_cast<const AstNode*>(n)->spec); });
    },
    /*clone*/
    [](const MongoExtensionAggStageAstNode* n, MongoExtensionAggStageAstNode** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new AstNode(static_cast<const AstNode*>(n)->spec); });
    },
    /*get_first_stage_view_application_policy*/
    [](const MongoExtensionAggStageAstNode*, MongoExtensionFirstStageViewApplicationPolicy* out) -> MongoExtensionStatus* { *out = kDefaultPrepend; return &g_ok; },
    /*bind_resolved_namespace*/ [](MongoExtensionAggStageAstNode*, const MongoExtensionResolvedNamespace*) -> MongoExtensionStatus* { return &g_ok; },
};

struct ExpandedContainer : MongoExtensionExpandedArrayContainer {
    std::vector<MongoExtensionAggStageAstNode*> nodes;
    static const MongoExtensionExpandedArrayContainerVTable vt;
    ExpandedContainer() : MongoExtensionExpandedArrayContainer{&vt} { g_liveObjs++; }
    ~ExpandedContainer() { g_liveObjs--; }
};
const MongoExtensionExpandedArrayContainerVTable ExpandedContainer::vt = {
    /*destroy*/
    [](MongoExtensionExpandedArrayContainer* c) {
        auto* e = static_cast<ExpandedContainer*>(c);
        for (auto* n : e->nodes) n->vtable->destroy(n);  // elements not transferred are still ours
        delete e;
    },
    /*size*/ [](const MongoExtensionExpandedArrayContainer* c) -> size_t { return static_cast<const ExpandedContainer*>(c)->nodes.size(); },
    /*transfer*/
    [](MongoExtensionExpandedArrayContainer* c, MongoExtensionExpandedArray* arr) -> MongoExtensionStatus* {
        return guard([&] {
            auto* e = static_cast<ExpandedContainer*>(c);
            if (arr->size != e->nodes.size()) throw ExtErr{20, "transfer(): output array has the wrong size"};
            for (size_t i = 0; i < e->nodes.size(); i++) {
                arr->elements[i].type = kAstNode;
                arr->elements[i].parseOrAst.ast = e->nodes[i];
            }
            e->nodes.clear();  // ownership moved to the host
        });
    },
};

struct ParseNode : MongoExtensionAggStageParseNode {
    Spec spec;
    static const MongoExtensionAggStageParseNodeVTable vt;
    explicit ParseNode(Spec s) : MongoExtensionAggStageParseNode{&vt}, spec(std::move(s)) { g_liveObjs++; }
    ~ParseNode() { g_liveObjs--; }
};
const MongoExtensionAggStageParseNodeVTable ParseNode::vt = {
    /*destroy*/ [](MongoExtensionAggStageParseNode* n) { delete static_cast<ParseNode*>(n); },
    /*get_name*/ [](const MongoExtensionAggStageParseNode* s) -> MongoExtensionByteView { const char* n = static_cast<const ParseNode*>(s)->spec.stageName(); return {reinterpret_cast<const uint8_t*>(n), std::strlen(n)}; },
    /*get_query_shape*/
    [](const MongoExtensionAggStageParseNode* n, const MongoExtensionHostQueryShapeOpts*, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] {  // POC simplification: shape is the stage name with an empty spec (no literal serialization via the host opts)
            bm::Builder inner, b;
            b.doc(static_cast<const ParseNode*>(n)->spec.stageName(), inner.finish());
            *out = new BufImpl(b.finish());
        });
    },
    /*expand*/
    [](const MongoExtensionAggStageParseNode* n, MongoExtensionExpandedArrayContainer** out) -> MongoExtensionStatus* {
        return guard([&] {
            auto* c = new ExpandedContainer();
            c->nodes.push_back(new AstNode(static_cast<const ParseNode*>(n)->spec));
            *out = c;
        });
    },
    /*clone*/
    [](const MongoExtensionAggStageParseNode* n, MongoExtensionAggStageParseNode** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new ParseNode(static_cast<const ParseNode*>(n)->spec); });
    },
    /*to_bson_for_log*/
    [](const MongoExtensionAggStageParseNode* n, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new BufImpl(static_cast<const ParseNode*>(n)->spec.toStageBson()); });
    },
};

struct Descriptor : MongoExtensionAggStageDescriptor {
    static const MongoExtensionAggStageDescriptorVTable vt;
    bool contains;
    explicit Descriptor(bool c) : MongoExtensionAggStageDescriptor{&vt}, contains(c) {}
};
const MongoExtensionAggStageDescriptorVTable Descriptor::vt = {
    /*get_name*/ [](const MongoExtensionAggStageDescriptor* d) -> MongoExtensionByteView { return static_cast<const Descriptor*>(d)->contains ? MongoExtensionByteView{reinterpret_cast<const uint8_t*>("$meshContains"), 13} : MongoExtensionByteView{reinterpret_cast<const uint8_t*>("$meshVolume"), 11}; },
    /*get_client_type*/ [](const MongoExtensionAggStageDescriptor*) -> MongoExtensionClientType { return kMongoExtensionClientTypeAny; },
    /*parse*/
    [](const MongoExtensionAggStageDescriptor* d, MongoExtensionByteView bson, MongoExtensionAggStageParseNode** out) -> MongoExtensionStatus* {
        return guard([&] {
            Spec s = Spec::fromStageBson(bson.data, bson.len);
            if (s.contains != static_cast<const Descriptor*>(d)->contains) throw ExtErr{12, "mesh stage: the specification names a different stage than the descriptor"};
            if (s.plant == "throwParse") throw std::runtime_error("planted C++ exception in parse");
            *out = new ParseNode(std::move(s));
        });
    },
};
Descriptor g_descriptor{false};
Descriptor g_descriptorContains{true};

const MongoExtensionVTable g_extVt = {
    /*initialize*/
    [](const MongoExtension*, const MongoExtensionHostPortal* portal) -> MongoExtensionStatus* {
        return guard([&] {
            const MongoExtensionByteView ov = portal->vtable->get_extension_options(portal);
            Config c = parseOptions(ov.data && ov.len ? string(reinterpret_cast<const char*>(ov.data), ov.len) : string());
#if defined(MESHVOL_MUTANT_CFG) && MESHVOL_MUTANT_CFG == 1
            c.gpu = false;  // mutant: option ignored
#elif defined(MESHVOL_MUTANT_CFG) && MESHVOL_MUTANT_CFG == 3
            c.gpuRequired = false;  // mutant: gpuRequired ignored
#endif
            applyGpuGate(c);
            g_cfg = c;
            consume(portal->vtable->register_stage_descriptor(portal, &g_descriptor));
            consume(portal->vtable->register_stage_descriptor(portal, &g_descriptorContains));
        });
    },
};
const MongoExtension g_extension = {&g_extVt, {MONGODB_EXTENSION_API_MAJOR_VERSION, MONGODB_EXTENSION_API_MINOR_VERSION}};
const MongoExtensionAPIVersion g_versions[] = {{MONGODB_EXTENSION_API_MAJOR_VERSION, MONGODB_EXTENSION_API_MINOR_VERSION}};
}  // namespace

extern "C" {
EXPORT void get_mongodb_extension_versions(MongoExtensionAPIVersionVector* v) {
    v->len = 1;
    v->versions = g_versions;
}
EXPORT MongoExtensionStatus* get_mongodb_extension(MongoExtensionAPIVersion version, const MongoExtensionHostServices* hs, const MongoExtension** out) {
    return guard([&] {
        if (version.major != MONGODB_EXTENSION_API_MAJOR_VERSION) throw ExtErr{21, "meshvolume: unsupported API major version requested"};
        g_hostServices = hs;
        *out = &g_extension;
    });
}
}
