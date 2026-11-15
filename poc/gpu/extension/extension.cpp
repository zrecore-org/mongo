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

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <dlfcn.h>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef MESHVOL_VULKAN_PROBE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#endif

#define EXPORT __attribute__((visibility("default")))

// Symbol-conflict probe: a plain global function that a host could also define (see host.cpp). With default visibility it would be an exported,
// interposable symbol; with -fvisibility=hidden + the export list it is not.
__attribute__((noinline)) int collide_me() { return 1; }

namespace {
using std::string;

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
    string input = "mesh", output = "meshVolume", plant, probe;
    uint32_t batchSize = 1024;
    int spinMs = 0;
    string raw;  // the validated sub-document, for serialize / clone

    static Spec fromStageBson(const uint8_t* d, size_t len) {
        if (!bm::validateDoc(d, len)) throw ExtErr{11, "$meshVolume: stage BSON is malformed"};
        bm::Elem e;
        if (!bm::findField(d, len, "$meshVolume", &e) || e.type != bm::kDoc)
            throw ExtErr{12, "$meshVolume: the stage specification must be a document"};
        Spec s = fromSpec(e.val, e.vlen);
        return s;
    }
    static Spec fromSpec(const uint8_t* d, size_t len) {
        if (!bm::validateDoc(d, len)) throw ExtErr{11, "$meshVolume: specification is malformed BSON"};
        Spec s;
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
        b.head(bm::kDoc, "$meshVolume");
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
};
using CancelFn = bool (*)(void*);

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

string vulkanProbe();
string countersDoc();

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
        string d = spec.probe == "vulkan" ? vulkanProbe() : spec.probe == "symbol" ? (bm::Builder().i32("collideMe", collide_me()).finish()) : countersDoc();
        for (size_t i = 0; i < n; i++) out[i].probeDoc = d;
        return true;
    }
    for (size_t i = 0; i < n; i++) {
        if ((i & 15) == 0 && cancel(cancelArg)) return false;
        DocOut& o = out[i];
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
    } else {
        bm::Builder w;
        w.str("exact", o.r.exact).dbl("approx", o.r.approx);
        sub = w.finish();
    }
    out.doc(spec.output, sub);
    return out.finish();
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
    void* h = nullptr;
    const char* cands[] = {"libvulkan.1.dylib", "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib", "libvulkan.so.1"};
    const char* used = "";
    for (const char* c : cands) if ((h = dlopen(c, RTLD_NOW | RTLD_LOCAL))) { used = c; break; }
    b.boolean("loaderLoaded", h != nullptr).str("loader", used);
    if (!h) { b.boolean("ok", false).str("detail", dlerror() ? dlerror() : "dlopen failed"); return b.finish(); }
    auto createInstance = (PFN_vkCreateInstance)dlsym(h, "vkCreateInstance");
    auto destroyInstance = (PFN_vkDestroyInstance)dlsym(h, "vkDestroyInstance");
    auto enumExt = (PFN_vkEnumerateInstanceExtensionProperties)dlsym(h, "vkEnumerateInstanceExtensionProperties");
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
    if (r == VK_SUCCESS) destroyInstance(inst, nullptr);
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
    /*get_name*/ [](const MongoExtensionExecAggStage*) -> MongoExtensionByteView { return {reinterpret_cast<const uint8_t*>("$meshVolume"), 11}; },
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
    /*get_name*/ [](const MongoExtensionLogicalAggStage*) -> MongoExtensionByteView { return {reinterpret_cast<const uint8_t*>("$meshVolume"), 11}; },
    /*serialize*/
    [](const MongoExtensionLogicalAggStage* s, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] { *out = new BufImpl(static_cast<const LogicalStage*>(s)->spec.toStageBson()); });
    },
    /*explain*/
    [](const MongoExtensionLogicalAggStage* s, MongoExtensionQueryExecutionContext*, MongoExtensionExplainVerbosity, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] {
            bm::Builder inner;
            inner.i32("batchSize", (int32_t)static_cast<const LogicalStage*>(s)->spec.batchSize);
            bm::Builder b;
            b.doc("$meshVolume", inner.finish());
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
    /*get_name*/ [](const MongoExtensionAggStageAstNode*) -> MongoExtensionByteView { return {reinterpret_cast<const uint8_t*>("$meshVolume"), 11}; },
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
    /*get_name*/ [](const MongoExtensionAggStageParseNode*) -> MongoExtensionByteView { return {reinterpret_cast<const uint8_t*>("$meshVolume"), 11}; },
    /*get_query_shape*/
    [](const MongoExtensionAggStageParseNode*, const MongoExtensionHostQueryShapeOpts*, MongoExtensionByteBuf** out) -> MongoExtensionStatus* {
        return guard([&] {  // POC simplification: shape is the stage name with an empty spec (no literal serialization via the host opts)
            bm::Builder inner, b;
            b.doc("$meshVolume", inner.finish());
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
    Descriptor() : MongoExtensionAggStageDescriptor{&vt} {}
};
const MongoExtensionAggStageDescriptorVTable Descriptor::vt = {
    /*get_name*/ [](const MongoExtensionAggStageDescriptor*) -> MongoExtensionByteView { return {reinterpret_cast<const uint8_t*>("$meshVolume"), 11}; },
    /*get_client_type*/ [](const MongoExtensionAggStageDescriptor*) -> MongoExtensionClientType { return kMongoExtensionClientTypeAny; },
    /*parse*/
    [](const MongoExtensionAggStageDescriptor*, MongoExtensionByteView bson, MongoExtensionAggStageParseNode** out) -> MongoExtensionStatus* {
        return guard([&] {
            Spec s = Spec::fromStageBson(bson.data, bson.len);
            if (s.plant == "throwParse") throw std::runtime_error("planted C++ exception in parse");
            *out = new ParseNode(std::move(s));
        });
    },
};
Descriptor g_descriptor;

const MongoExtensionVTable g_extVt = {
    /*initialize*/
    [](const MongoExtension*, const MongoExtensionHostPortal* portal) -> MongoExtensionStatus* {
        return guard([&] {
            MongoExtensionStatus* s = portal->vtable->register_stage_descriptor(portal, &g_descriptor);
            consume(s);
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
