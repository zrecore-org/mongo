// MP14a POC: fake extension host + tests. Implements just enough of the host side of the Extensions API (public/api.h v1.0):
// loading (mirrors host/load_extension.cpp), a portal, host services, a source stage, a query execution context (deadline, interrupt) and the
// full stage lifecycle (parse, expand, promote, compile, set_source, open, get_next, close, destroy) with strict ownership accounting.
// The exact oracle is oracle/exact.h (verbatim copy from zrecore-org/exactmesh, Apache-2.0). This file never includes the extension's own volume code.
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <cstring>
#include "mongo/db/extension/public/api.h"

#include "bson_min.h"
#include "meshcontains_core.h"
#include "oracle/exact.h"
#include "oracle/mesh_io.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <tuple>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <functional>
#include <sys/resource.h>
#include <thread>
#include <random>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using std::string;
using std::vector;
using boost::multiprecision::cpp_int;
using boost::multiprecision::cpp_rational;

// A symbol the extension also defines (non-hidden build): used by the symbol-conflict probe. Exported from the host with -Wl,-export_dynamic.
int collide_me() { return 2; }

namespace {
int g_fail = 0, g_pass = 0;
std::atomic<long> g_escapes{0};  // C++ exceptions that crossed an extension call
void result(const string& name, bool ok, const string& detail = "") {
    (ok ? g_pass : g_fail)++;
    std::printf("RESULT %-52s %s %s\n", name.c_str(), ok ? "PASS" : "FAIL", detail.c_str());
    std::fflush(stdout);
}
int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// ------------------------------------------------------------------ host-side statuses and buffers
struct HStatus : MongoExtensionStatus {
    int32_t code;
    string reason;
    static const MongoExtensionStatusVTable vt;
    HStatus(int32_t c, string r) : MongoExtensionStatus{&vt}, code(c), reason(std::move(r)) {}
};
const MongoExtensionStatusVTable HStatus::vt = {
    [](MongoExtensionStatus* s) { delete static_cast<HStatus*>(s); },
    [](const MongoExtensionStatus* s) -> int32_t { return static_cast<const HStatus*>(s)->code; },
    [](const MongoExtensionStatus* s) -> MongoExtensionByteView { auto& r = static_cast<const HStatus*>(s)->reason; return {reinterpret_cast<const uint8_t*>(r.data()), r.size()}; },
    [](MongoExtensionStatus* s, int32_t c) { static_cast<HStatus*>(s)->code = c; },
    [](MongoExtensionStatus* s, MongoExtensionByteView v) -> MongoExtensionStatus* { static_cast<HStatus*>(s)->reason.assign(reinterpret_cast<const char*>(v.data), v.len); return nullptr; },
    [](const MongoExtensionStatus* s, MongoExtensionStatus** out) -> MongoExtensionStatus* { auto* h = static_cast<const HStatus*>(s); *out = new HStatus(h->code, h->reason); return nullptr; },
};
MongoExtensionStatus* hostOk() {  // singleton OK with no-op destroy, same convention the real host uses
    static const MongoExtensionStatusVTable vt = {
        [](MongoExtensionStatus*) {}, [](const MongoExtensionStatus*) -> int32_t { return 0; },
        [](const MongoExtensionStatus*) -> MongoExtensionByteView { return {nullptr, 0}; }, [](MongoExtensionStatus*, int32_t) {},
        [](MongoExtensionStatus*, MongoExtensionByteView) -> MongoExtensionStatus* { return nullptr; },
        [](const MongoExtensionStatus* s, MongoExtensionStatus** out) -> MongoExtensionStatus* { *out = const_cast<MongoExtensionStatus*>(s); return nullptr; }};
    static MongoExtensionStatus s{&vt};
    return &s;
}
string sv(MongoExtensionByteView v) { return string(reinterpret_cast<const char*>(v.data), v.len); }
MongoExtensionByteView bv(const string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

struct Status { int code = 0; string reason; bool escaped = false; bool nullStatus = false; };
// Consumes (destroys) a status returned by an extension call. Runs the call inside try/catch to count C++ exceptions that escape the boundary.
Status take(const std::function<MongoExtensionStatus*()>& f) {
    Status r;
    MongoExtensionStatus* s = nullptr;
    try { s = f(); } catch (...) { g_escapes++; r.escaped = true; r.code = -999; r.reason = "C++ exception escaped the extension call"; return r; }
    if (!s) { r.nullStatus = true; return r; }
    r.code = s->vtable->get_code(s);
    r.reason = sv(s->vtable->get_reason(s));
    s->vtable->destroy(s);
    return r;
}

// ------------------------------------------------------------------ host services, portal
MongoExtensionLogger g_logger{nullptr};
std::atomic<long> g_logMsgs{0};
const MongoExtensionLoggerVTable g_loggerVt = {
    [](const MongoExtensionLogMessage*) -> MongoExtensionStatus* { g_logMsgs++; return hostOk(); },
    [](MongoExtensionLogSeverity, MongoExtensionLogType, bool* out) -> MongoExtensionStatus* { *out = true; return hostOk(); },
};
MongoExtensionStatus* unsupported(const char* what) { return new HStatus(1, string("fake host: ") + what + " not implemented"); }
const MongoExtensionHostServicesVTable g_hsVt = {
    []() -> MongoExtensionLogger* { return new MongoExtensionLogger{&g_loggerVt}; },
    [](MongoExtensionByteView m) -> MongoExtensionStatus* { return new HStatus(2, "user_asserted: " + sv(m)); },
    [](MongoExtensionByteView m) -> MongoExtensionStatus* { return new HStatus(3, "tripwire_asserted: " + sv(m)); },
    [](MongoExtensionIdleThreadBlock**, const char*) -> MongoExtensionStatus* { return unsupported("mark_idle_thread_block"); },
    [](MongoExtensionByteView, MongoExtensionAggStageParseNode**) -> MongoExtensionStatus* { return unsupported("create_host_agg_stage_parse_node"); },
    [](MongoExtensionByteView, MongoExtensionAggStageAstNode**) -> MongoExtensionStatus* { return unsupported("create_id_lookup"); },
    [](MongoExtensionByteView, MongoExtensionDocResultsDPLCallback, void*, void (*)(void*), MongoExtensionAggStageAstNode**) -> MongoExtensionStatus* { return unsupported("create_document_results_and_metadata"); },
};
MongoExtensionHostServices g_hostServices{&g_hsVt};

// MP14c: number of loaded images whose path looks like a Vulkan loader or driver (independent of the extension's own counter).
int vulkanImages() {
    int n = 0;
#ifdef __APPLE__
    for (uint32_t i = 0; i < _dyld_image_count(); i++) { const char* nm = _dyld_get_image_name(i); if (nm && (strstr(nm, "libvulkan") || strstr(nm, "MoltenVK"))) n++; }
#else
    FILE* f = fopen("/proc/self/maps", "r");
    char line[4096];
    while (f && fgets(line, sizeof line, f)) if (strstr(line, "libvulkan") || strstr(line, "libMoltenVK")) n++;
    if (f) fclose(f);
#endif
    return n;
}
string g_extOptions;  // MP14c: the extensionOptions text the portal hands to initialize()
struct HPortal : MongoExtensionHostPortal {
    vector<const MongoExtensionAggStageDescriptor*> registered;
    static const MongoExtensionHostPortalVTable vt;
    HPortal() : MongoExtensionHostPortal{&vt, {1, 0}, 0} {}
};
const MongoExtensionHostPortalVTable HPortal::vt = {
    [](const MongoExtensionHostPortal* p, const MongoExtensionAggStageDescriptor* d) -> MongoExtensionStatus* {
        const_cast<HPortal*>(static_cast<const HPortal*>(p))->registered.push_back(d);
        return hostOk();
    },
    [](const MongoExtensionHostPortal*) -> MongoExtensionByteView { return {reinterpret_cast<const uint8_t*>(g_extOptions.data()), g_extOptions.size()}; },
    [](const MongoExtensionHostPortal*, MongoExtensionByteView, const MongoExtensionPipelineRewriteRule*, size_t) -> MongoExtensionStatus* { return hostOk(); },
};

// ------------------------------------------------------------------ loading: mirrors host/load_extension.cpp (negotiateExtensionVersion, selectCompatibleVersion, getMongoExtension)
struct ExtLib {
    void* h = nullptr;
    string path, error, stageName;
    MongoExtensionAPIVersion negotiated{0, 0};
    vector<MongoExtensionAPIVersion> advertised;
    const MongoExtension* ext = nullptr;
    const MongoExtensionAggStageDescriptor* desc = nullptr;
    const MongoExtensionAggStageDescriptor* descContains = nullptr;   // MP14d
    bool ok = false;
};
const MongoExtensionAPIVersion kHostVersions[] = {{1, 0}};

bool selectCompatible(const vector<MongoExtensionAPIVersion>& ext, MongoExtensionAPIVersion* out, string* err) {
    if (ext.empty()) { *err = "the extension published an empty API version list"; return false; }  // load_extension.cpp:152-157
    auto sorted = ext;
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return std::tie(a.major, a.minor) > std::tie(b.major, b.minor); });
    bool sawMajor = false;
    for (auto& e : sorted)
        for (auto& h : kHostVersions)
            if (h.major == e.major) { sawMajor = true; if (h.minor >= e.minor) { *out = e; return true; } }
    *err = sawMajor ? "extension's API minor version exceeds the host's maximum supported minor for every matching major"
                    : "no API major version published by the extension matches a host-supported major version";
    return false;
}

ExtLib loadExtension(const string& path) {
    ExtLib L;
    L.path = path;
    L.h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);  // same flags as src/mongo/platform/shared_library_posix.cpp:39
    if (!L.h) { L.error = string("dlopen failed: ") + dlerror(); return L; }
    auto getVersions = (get_mongodb_extension_versions_t)dlsym(L.h, GET_MONGODB_EXTENSION_VERSIONS_SYMBOL);
    if (!getVersions) { L.error = "symbol get_mongodb_extension_versions not found"; return L; }
    MongoExtensionAPIVersionVector vec{0, nullptr};
    try { getVersions(&vec); } catch (...) { g_escapes++; L.error = "exception escaped get_mongodb_extension_versions"; return L; }
    if (!vec.versions && vec.len) { L.error = "null version list"; return L; }
    for (uint64_t i = 0; i < vec.len; i++) L.advertised.push_back(vec.versions[i]);
    if (!vec.versions) { L.error = "the extension published a null API version list"; return L; }
    string err;
    if (!selectCompatible(L.advertised, &L.negotiated, &err)) { L.error = err; return L; }
    auto getExt = (get_mongo_extension_t)dlsym(L.h, GET_MONGODB_EXTENSION_SYMBOL);
    if (!getExt) { L.error = "symbol get_mongodb_extension not found"; return L; }
    Status st = take([&] { return getExt(L.negotiated, &g_hostServices, &L.ext); });
    if (st.code != 0 || st.escaped) { L.error = "get_mongodb_extension failed: " + st.reason; return L; }
    if (!L.ext) { L.error = "get_mongodb_extension failed to set an extension"; return L; }
    if (L.ext->version.major != L.negotiated.major || L.ext->version.minor != L.negotiated.minor) {
        L.error = "returned extension version " + std::to_string(L.ext->version.major) + "." + std::to_string(L.ext->version.minor) + " did not match negotiated version " +
                  std::to_string(L.negotiated.major) + "." + std::to_string(L.negotiated.minor);
        return L;
    }
    HPortal portal;
    st = take([&] { return L.ext->vtable->initialize(L.ext, &portal); });
    if (st.code != 0 || st.escaped) { L.error = "initialize failed: " + st.reason; return L; }
    if (portal.registered.size() != 2) { L.error = "expected two registered stages ($meshVolume, $meshContains), got " + std::to_string(portal.registered.size()); return L; }
    L.desc = portal.registered[0];
    L.descContains = portal.registered[1];
    L.stageName = sv(L.desc->vtable->get_name(L.desc));
    L.ok = true;
    return L;
}

// ------------------------------------------------------------------ query execution context and source stage
struct HostCtx : MongoExtensionQueryExecutionContext {
    int64_t deadlineMs = INT64_MAX;
    std::atomic<bool> killed{false};
    std::atomic<long> interruptChecks{0};
    static const MongoExtensionQueryExecutionContextVTable vt;
    HostCtx() : MongoExtensionQueryExecutionContext{&vt} {}
};
const MongoExtensionQueryExecutionContextVTable HostCtx::vt = {
    [](const MongoExtensionQueryExecutionContext* c, MongoExtensionStatus* q) -> MongoExtensionStatus* {
        auto* h = const_cast<HostCtx*>(static_cast<const HostCtx*>(c));
        h->interruptChecks++;
        if (h->killed) { q->vtable->set_code(q, 11601); q->vtable->set_reason(q, bv("operation was interrupted (fake host killOp)")); }
        return hostOk();
    },
    [](const MongoExtensionQueryExecutionContext*, MongoExtensionExecAggStage*, MongoExtensionOperationMetrics**) -> MongoExtensionStatus* { return unsupported("get_metrics"); },
    [](const MongoExtensionQueryExecutionContext* c, int64_t* out) -> MongoExtensionStatus* { *out = static_cast<const HostCtx*>(c)->deadlineMs; return hostOk(); },
    [](const MongoExtensionQueryExecutionContext*, const MongoExtensionByteView*, uint64_t, MongoExtensionByteBuf**) -> MongoExtensionStatus* { return unsupported("get_host_metrics"); },
    [](const MongoExtensionQueryExecutionContext*, uint64_t) -> MongoExtensionStatus* { return hostOk(); },
};

// Source stage. Hands out documents as views that are valid only until the next get_next(), and poisons the previous bytes to catch an extension that retains a view.
struct SourceStage : MongoExtensionExecAggStage {
    const vector<string>* docs;
    size_t i = 0;
    vector<uint8_t> cur;
    std::atomic<long> pulls{0};
    static const MongoExtensionExecAggStageVTable vt;
    explicit SourceStage(const vector<string>* d) : MongoExtensionExecAggStage{&vt}, docs(d) {}
};
const MongoExtensionExecAggStageVTable SourceStage::vt = {
    [](MongoExtensionExecAggStage*) {},
    [](MongoExtensionExecAggStage* s, MongoExtensionQueryExecutionContext*, MongoExtensionGetNextResult* r) -> MongoExtensionStatus* {
        auto* S = static_cast<SourceStage*>(s);
        S->pulls++;
        std::memset(S->cur.data(), 0xDD, S->cur.size());
        r->resultMetadata.type = kByteView; r->resultMetadata.bytes.view = {nullptr, 0};
        if (S->i >= S->docs->size()) { r->code = kEOF; r->resultDocument.type = kByteView; r->resultDocument.bytes.view = {nullptr, 0}; return hostOk(); }
        const string& d = (*S->docs)[S->i++];
        S->cur.assign(d.begin(), d.end());
        r->code = kAdvanced; r->resultDocument.type = kByteView; r->resultDocument.bytes.view = {S->cur.data(), S->cur.size()};
        return hostOk();
    },
    [](const MongoExtensionExecAggStage*) -> MongoExtensionByteView { return {reinterpret_cast<const uint8_t*>("fakeSource"), 10}; },
    [](const MongoExtensionExecAggStage*, MongoExtensionOperationMetrics**) -> MongoExtensionStatus* { return hostOk(); },
    [](MongoExtensionExecAggStage*, MongoExtensionExecAggStage*) -> MongoExtensionStatus* { return unsupported("set_source on source"); },
    [](MongoExtensionExecAggStage*) -> MongoExtensionStatus* { return hostOk(); },
    [](MongoExtensionExecAggStage*) -> MongoExtensionStatus* { return hostOk(); },
    [](MongoExtensionExecAggStage*) -> MongoExtensionStatus* { return hostOk(); },
    [](const MongoExtensionExecAggStage*, MongoExtensionQueryExecutionContext*, MongoExtensionExplainVerbosity, MongoExtensionByteBuf**) -> MongoExtensionStatus* { return unsupported("explain"); },
};

// ------------------------------------------------------------------ pipeline runner (full lifecycle with ownership accounting)
struct RunOut {
    bool ok = false;
    Status err;
    string failedAt;
    vector<string> docs;
    double ms = 0;
    long hostGetNext = 0, sourcePulls = 0;
    string properties;
};
string stageBson(const string& specDoc) { bm::Builder b; b.head(bm::kDoc, "$meshVolume"); b.b.append(specDoc); return b.finish(); }
string spec(const std::function<void(bm::Builder&)>& f) { bm::Builder b; f(b); return b.finish(); }

RunOut runPipeline(const ExtLib& L, const string& stage, const vector<string>& input, HostCtx& ctx, const MongoExtensionAggStageDescriptor* dsc = nullptr) {
    if (!dsc) dsc = L.desc;
    RunOut R;
    const auto t0 = std::chrono::steady_clock::now();
    auto fail = [&](const char* where, Status s) { R.failedAt = where; R.err = s; return s.code != 0 || s.escaped; };
    MongoExtensionAggStageParseNode* parse = nullptr;
    Status st = take([&] { return dsc->vtable->parse(dsc, bv(stage), &parse); });
    if (fail("parse", st)) return R;
    MongoExtensionExpandedArrayContainer* cont = nullptr;
    st = take([&] { return parse->vtable->expand(parse, &cont); });
    if (fail("expand", st)) { parse->vtable->destroy(parse); return R; }
    size_t n = cont->vtable->size(cont);
    vector<MongoExtensionExpandedArrayElement> els(n);
    MongoExtensionExpandedArray arr{n, els.data()};
    st = take([&] { return cont->vtable->transfer(cont, &arr); });
    cont->vtable->destroy(cont);
    parse->vtable->destroy(parse);
    if (fail("transfer", st)) return R;
    if (n != 1 || els[0].type != kAstNode) { R.failedAt = "expand shape"; R.err = {1, "expected one AST node"}; return R; }
    MongoExtensionAggStageAstNode* ast = els[0].parseOrAst.ast;
    MongoExtensionByteBuf* props = nullptr;
    st = take([&] { return ast->vtable->get_properties(ast, &props); });
    if (fail("get_properties", st)) { ast->vtable->destroy(ast); return R; }
    if (props) { auto v = props->vtable->get_view(props); R.properties = bm::validateDoc(v.data, v.len) ? "valid BSON" : "INVALID BSON"; props->vtable->destroy(props); }
    MongoExtensionCatalogContext cc{{{reinterpret_cast<const uint8_t*>("db"), 2}, {reinterpret_cast<const uint8_t*>("coll"), 4}}, {nullptr, 0}, 0, kNotExplain, {nullptr, 0}, 0};
    MongoExtensionLogicalAggStage* logical = nullptr;
    st = take([&] { return ast->vtable->promote(ast, &cc, &logical); });
    ast->vtable->destroy(ast);
    if (fail("promote", st)) return R;
    MongoExtensionExecAggStage* exec = nullptr;
    st = take([&] { return logical->vtable->compile(logical, &exec); });
    logical->vtable->destroy(logical);
    if (fail("compile", st)) return R;
    SourceStage source(&input);
    st = take([&] { return exec->vtable->set_source(exec, &source); });
    if (!fail("set_source", st)) st = take([&] { return exec->vtable->open(exec); });
    bool bad = fail("open", st) && R.failedAt == "open";
    if (!bad && R.err.code == 0 && !R.err.escaped) {
        while (true) {
            MongoExtensionGetNextResult r{};
            r.resultDocument.type = kByteView; r.resultDocument.bytes.view = {nullptr, 0};
            r.resultMetadata.type = kByteView; r.resultMetadata.bytes.view = {nullptr, 0};
            R.hostGetNext++;
            st = take([&] { return exec->vtable->get_next(exec, &ctx, &r); });
            if (st.code != 0 || st.escaped) { fail("get_next", st); break; }
            if (r.code == kEOF) { R.ok = true; break; }
            if (r.code == kPauseExecution) continue;
            MongoExtensionByteView v;
            if (r.resultDocument.type == kByteBuf) {
                v = r.resultDocument.bytes.buf->vtable->get_view(r.resultDocument.bytes.buf);
                R.docs.emplace_back(reinterpret_cast<const char*>(v.data), v.len);
                r.resultDocument.bytes.buf->vtable->destroy(r.resultDocument.bytes.buf);
            } else {
                v = r.resultDocument.bytes.view;
                R.docs.emplace_back(reinterpret_cast<const char*>(v.data), v.len);
            }
            if (r.resultMetadata.type == kByteBuf && r.resultMetadata.bytes.buf) r.resultMetadata.bytes.buf->vtable->destroy(r.resultMetadata.bytes.buf);
        }
        take([&] { return exec->vtable->close(exec); });
    }
    exec->vtable->destroy(exec);
    R.sourcePulls = source.pulls;
    R.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return R;
}
RunOut run(const ExtLib& L, const string& specDoc, const vector<string>& input) { HostCtx c; return runPipeline(L, stageBson(specDoc), input, c); }
string stageBsonContains(const string& specDoc) { bm::Builder b; b.head(bm::kDoc, "$meshContains"); b.b.append(specDoc); return b.finish(); }
RunOut runContains(const ExtLib& L, const string& specDoc, const vector<string>& input) { HostCtx c; return runPipeline(L, stageBsonContains(specDoc), input, c, L.descContains); }

// ------------------------------------------------------------------ BSON helpers for tests
string packMesh(const mesh::Mesh& m) {
    string s;
    uint32_t h[3] = {mesh::kMagicMesh, (uint32_t)m.nv(), (uint32_t)m.nt()};
    s.append(reinterpret_cast<const char*>(h), 12);
    s.append(reinterpret_cast<const char*>(m.v.data()), m.v.size() * 8);
    s.append(reinterpret_cast<const char*>(m.t.data()), m.t.size() * 4);
    return s;
}
string docOf(int id, const string& packed, const char* field = "mesh") { bm::Builder b; b.i32("_id", id); b.bin(field, packed.data(), packed.size()); return b.finish(); }

struct Out { bool hasErr = false; int errCode = 0; string exact; double approx = 0; bool ok = false; int id = -1; };
Out parseOut(const string& d, const char* field = "meshVolume") {
    Out o;
    bm::Elem e;
    auto* p = reinterpret_cast<const uint8_t*>(d.data());
    if (!bm::validateDoc(p, d.size())) return o;
    if (bm::findField(p, d.size(), "_id", &e) && e.type == bm::kInt32) std::memcpy(&o.id, e.val, 4);
    if (!bm::findField(p, d.size(), field, &e) || e.type != bm::kDoc) return o;
    const uint8_t* q = e.val + 4;
    const uint8_t* end = e.val + e.vlen - 1;
    while (q < end) {
        bm::Elem f;
        if (!bm::readElem(q, end, &f)) return o;
        q = f.end;
        if (f.name == "exact" && f.type == bm::kString) o.exact.assign(reinterpret_cast<const char*>(f.val + 4), f.vlen - 5);
        if (f.name == "approx" && f.type == bm::kDouble) std::memcpy(&o.approx, f.val, 8);
        if (f.name == "error" && f.type == bm::kDoc) {
            o.hasErr = true;
            const uint8_t* r = f.val + 4; const uint8_t* rend = f.val + f.vlen - 1;
            while (r < rend) { bm::Elem g; if (!bm::readElem(r, rend, &g)) break; r = g.end; if (g.name == "code" && g.type == bm::kInt32) std::memcpy(&o.errCode, g.val, 4); }
        }
        if (f.name == "collideMe" && f.type == bm::kInt32) std::memcpy(&o.errCode, f.val, 4);
    }
    o.ok = !o.hasErr && !o.exact.empty();
    return o;
}

// ------------------------------------------------------------------ oracle comparison (exact)
cpp_rational ratOfDouble(double x) {
    if (x == 0) return 0;
    int e;
    double f = std::frexp(x, &e);
    int64_t m = (int64_t)std::ldexp(f, 53);
    e -= 53;
    cpp_rational r(m);
    if (e >= 0) r *= (cpp_int(1) << e); else r /= (cpp_int(1) << -e);
    return r;
}
cpp_rational oracleVolume(const mesh::Mesh& m) {
    mesh::Dyadic d = mesh::exactVolumeTimes6(m);
    cpp_rational r(d.num);
    if (d.exp2 >= 0) r *= (cpp_int(1) << d.exp2); else r /= (cpp_int(1) << -d.exp2);
    r /= 6;
    return r;
}
string ratStr(const cpp_rational& r) { return r.str(); }
// d is the correctly rounded (nearest, ties to even) binary64 of r, proven by exact comparison with both neighbours.
bool correctlyRounded(const cpp_rational& r, double d) {
    if (!std::isfinite(d)) return false;
    auto absr = [](cpp_rational x) { return x < 0 ? -x : x; };
    const cpp_rational dd = absr(r - ratOfDouble(d));
    for (double nb : {std::nextafter(d, INFINITY), std::nextafter(d, -INFINITY)}) {
        if (!std::isfinite(nb)) continue;
        const cpp_rational dn = absr(r - ratOfDouble(nb));
        if (dn < dd) return false;
        if (dn == dd) { uint64_t u; std::memcpy(&u, &d, 8); if (u & 1) return false; }  // tie: must be the even mantissa
    }
    return true;
}

// ------------------------------------------------------------------ mesh generators (deterministic)
const double kPi = 3.14159265358979323846;
void orientOutward(mesh::Mesh& m) {  // flip all triangles if the (floating) signed volume is negative; consistency of orientation is preserved
    double s = 0;
    for (size_t k = 0; k < m.nt(); k++) {
        const double *a = &m.v[3 * m.t[3 * k]], *b = &m.v[3 * m.t[3 * k + 1]], *c = &m.v[3 * m.t[3 * k + 2]];
        s += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
    if (s < 0) for (size_t k = 0; k < m.nt(); k++) std::swap(m.t[3 * k + 1], m.t[3 * k + 2]);
}
mesh::Mesh box(double lx, double ly, double lz, double hx, double hy, double hz) {
    mesh::Mesh m;
    for (int i = 0; i < 8; i++) m.addV((i & 1) ? hx : lx, (i & 2) ? hy : ly, (i & 4) ? hz : lz);
    static const uint32_t T[12][3] = {{0, 2, 3}, {0, 3, 1}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4}, {2, 6, 7}, {2, 7, 3}, {0, 4, 6}, {0, 6, 2}, {1, 3, 7}, {1, 7, 5}};
    for (auto& t : T) m.addT(t[0], t[1], t[2]);
    orientOutward(m);
    return m;
}
mesh::Mesh uvSphere(int nlat, int nlon, double r, double cx, double cy, double cz) {
    mesh::Mesh m;
    m.addV(cx, cy, cz + r);
    for (int i = 1; i < nlat; i++)
        for (int j = 0; j < nlon; j++) {
            const double th = kPi * i / nlat, ph = 2 * kPi * j / nlon;
            m.addV(cx + r * std::sin(th) * std::cos(ph), cy + r * std::sin(th) * std::sin(ph), cz + r * std::cos(th));
        }
    const uint32_t south = m.addV(cx, cy, cz - r);
    auto id = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * nlon + (j % nlon)); };
    for (int j = 0; j < nlon; j++) m.addT(0, id(1, j), id(1, j + 1));
    for (int i = 1; i < nlat - 1; i++)
        for (int j = 0; j < nlon; j++) { m.addT(id(i, j), id(i + 1, j), id(i + 1, j + 1)); m.addT(id(i, j), id(i + 1, j + 1), id(i, j + 1)); }
    for (int j = 0; j < nlon; j++) m.addT(south, id(nlat - 1, j + 1), id(nlat - 1, j));
    orientOutward(m);
    return m;
}
mesh::Mesh torus(int nu, int nv, double R, double r, double cx, double cy, double cz) {
    mesh::Mesh m;
    for (int i = 0; i < nu; i++)
        for (int j = 0; j < nv; j++) {
            const double u = 2 * kPi * i / nu, v = 2 * kPi * j / nv;
            m.addV(cx + (R + r * std::cos(v)) * std::cos(u), cy + (R + r * std::cos(v)) * std::sin(u), cz + r * std::sin(v));
        }
    auto id = [&](int i, int j) { return (uint32_t)((i % nu) * nv + (j % nv)); };
    for (int i = 0; i < nu; i++)
        for (int j = 0; j < nv; j++) { m.addT(id(i, j), id(i + 1, j), id(i + 1, j + 1)); m.addT(id(i, j), id(i + 1, j + 1), id(i, j + 1)); }
    orientOutward(m);
    return m;
}
mesh::Mesh nested(const mesh::Mesh& outer, mesh::Mesh inner) {  // cavity: the inner shell reversed
    for (size_t k = 0; k < inner.nt(); k++) std::swap(inner.t[3 * k + 1], inner.t[3 * k + 2]);
    mesh::Mesh m = outer;
    const uint32_t off = (uint32_t)outer.nv();
    m.v.insert(m.v.end(), inner.v.begin(), inner.v.end());
    for (uint32_t i : inner.t) m.t.push_back(i + off);
    return m;
}
mesh::Mesh tetra(std::mt19937_64& g) {
    std::uniform_real_distribution<double> u(0.0, 10.0);
    mesh::Mesh m;
    for (int i = 0; i < 4; i++) m.addV(u(g), u(g), u(g));
    m.addT(0, 2, 1); m.addT(0, 1, 3); m.addT(0, 3, 2); m.addT(1, 2, 3);
    return m;  // orientation consistent; sign may be either: volume sign is part of the exact result
}
// Random closed mesh: subdivided octahedron, every vertex jittered radially and in position with full-mantissa binary64, then scaled and translated.
mesh::Mesh randomClosed(std::mt19937_64& g, int levels) {
    mesh::Mesh m;
    m.addV(1, 0, 0); m.addV(-1, 0, 0); m.addV(0, 1, 0); m.addV(0, -1, 0); m.addV(0, 0, 1); m.addV(0, 0, -1);
    vector<std::array<uint32_t, 3>> tris = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    for (int l = 0; l < levels; l++) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            auto key = std::minmax(a, b);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            double x = (m.v[3 * a] + m.v[3 * b]) / 2, y = (m.v[3 * a + 1] + m.v[3 * b + 1]) / 2, z = (m.v[3 * a + 2] + m.v[3 * b + 2]) / 2;
            double n = std::sqrt(x * x + y * y + z * z);
            return mid[key] = m.addV(x / n, y / n, z / n);
        };
        vector<std::array<uint32_t, 3>> nt;
        for (auto& t : tris) {
            uint32_t ab = midpoint(t[0], t[1]), bc = midpoint(t[1], t[2]), ca = midpoint(t[2], t[0]);
            nt.push_back({t[0], ab, ca}); nt.push_back({ab, t[1], bc}); nt.push_back({ca, bc, t[2]}); nt.push_back({ab, bc, ca});
        }
        tris = nt;
    }
    std::uniform_real_distribution<double> rad(0.6, 1.4), jit(-1e-3, 1e-3), sc(-20, 20), off(-1.0, 1.0);
    const double scale = std::ldexp(1.0 + rad(g) * 0.123456789, (int)sc(g));
    const double ox = off(g) * 1e8 * (g() & 1), oy = off(g) * 1e8 * (g() & 1), oz = off(g) * 1e3;
    for (size_t i = 0; i < m.nv(); i++) {
        const double r = rad(g);
        m.v[3 * i] = (m.v[3 * i] * r + jit(g)) * scale + ox;
        m.v[3 * i + 1] = (m.v[3 * i + 1] * r + jit(g)) * scale + oy;
        m.v[3 * i + 2] = (m.v[3 * i + 2] * r + jit(g)) * scale + oz;
    }
    for (auto& t : tris) m.addT(t[0], t[1], t[2]);
    return m;
}

// ------------------------------------------------------------------ counters probe
struct Counters { long computeCalls = 0, sourcePulls = 0, getNextCalls = 0, throwsCaught = 0, maxBatch = 0, liveBufs = 0, liveStatuses = 0, liveObjs = 0, docsOut = 0; bool ok = false; };
Counters counters(const ExtLib& L) {
    Counters c;
    auto r = run(L, spec([](bm::Builder& b) { b.str("probe", "counters"); }), {docOf(0, "")});
    if (!r.ok || r.docs.size() != 1) return c;
    bm::Elem e;
    auto* p = reinterpret_cast<const uint8_t*>(r.docs[0].data());
    if (!bm::findField(p, r.docs[0].size(), "meshVolume", &e)) return c;
    const uint8_t* q = e.val + 4; const uint8_t* end = e.val + e.vlen - 1;
    while (q < end) {
        bm::Elem f; if (!bm::readElem(q, end, &f)) break; q = f.end;
        int64_t v = 0; if (f.type == bm::kInt64) std::memcpy(&v, f.val, 8);
        if (f.name == "computeCalls") c.computeCalls = v; else if (f.name == "sourcePulls") c.sourcePulls = v; else if (f.name == "getNextCalls") c.getNextCalls = v;
        else if (f.name == "throwsCaught") c.throwsCaught = v; else if (f.name == "maxBatch") c.maxBatch = v; else if (f.name == "liveBufs") c.liveBufs = v;
        else if (f.name == "liveStatuses") c.liveStatuses = v; else if (f.name == "liveObjs") c.liveObjs = v; else if (f.name == "docsOut") c.docsOut = v;
    }
    c.ok = true;
    return c;
}

// ------------------------------------------------------------------ the correctness corpus (bars 3 and 6)
struct CorpusStats { long total = 0, wrong = 0, wrongExact = 0, wrongRound = 0, errors = 0; };
void checkMeshes(const ExtLib& L, const string& group, const vector<mesh::Mesh>& meshes, CorpusStats* cs, int batch = 4096) {
    vector<string> docs;
    for (size_t i = 0; i < meshes.size(); i++) docs.push_back(docOf((int)i, packMesh(meshes[i])));
    auto r = run(L, spec([&](bm::Builder& b) { b.i32("batchSize", batch); }), docs);
    long wrong = 0, wrongExact = 0, wrongRound = 0;
    if (!r.ok || r.docs.size() != meshes.size()) { result("corpus " + group + " ran", false, "pipeline failed: " + r.err.reason); cs->wrong += meshes.size(); cs->total += meshes.size(); return; }
    for (size_t i = 0; i < meshes.size(); i++) {
        Out o = parseOut(r.docs[i]);
        const cpp_rational want = oracleVolume(meshes[i]);
        bool bad = false;
        if (o.id != (int)i || !o.ok) { bad = true; cs->errors++; wrongExact++; }
        else {
            if (o.exact != ratStr(want)) { bad = true; wrongExact++; }
            else if (!correctlyRounded(want, o.approx)) { bad = true; wrongRound++; }
        }
        if (bad) wrong++;
    }
    cs->total += meshes.size(); cs->wrong += wrong; cs->wrongExact += wrongExact; cs->wrongRound += wrongRound;
    result("corpus " + group, wrong == 0, "meshes=" + std::to_string(meshes.size()) + " wrong=" + std::to_string(wrong) + " (exactMismatch=" + std::to_string(wrongExact) + " roundingWrong=" + std::to_string(wrongRound) + ")");
}

CorpusStats runCorpus(const ExtLib& L) {
    CorpusStats cs;
    std::mt19937_64 g(20261114);
    std::uniform_real_distribution<double> u(-3, 3), pos(0.1, 5);
    vector<mesh::Mesh> boxes, spheres, tori, nest, rnd, small;
    for (int i = 0; i < 200; i++) {
        const double lx = u(g), ly = u(g), lz = u(g);
        boxes.push_back(box(lx, ly, lz, lx + pos(g), ly + pos(g), lz + pos(g)));
    }
    for (int i = 0; i < 40; i++) boxes.push_back(box(i, 2 * i + 1, -i, i + 1 + (i % 5), 2 * i + 3, -i + 2));  // small-integer boxes
    for (int i = 0; i < 100; i++) spheres.push_back(uvSphere(3 + i % 12, 4 + i % 17, pos(g), (i % 3 == 0) ? 1e8 : u(g), u(g), u(g)));
    for (int i = 0; i < 100; i++) tori.push_back(torus(5 + i % 11, 4 + i % 9, 3 + pos(g), 0.5 + 0.1 * (i % 5), (i % 2) ? 1e7 : 0.0, u(g), u(g)));
    for (int i = 0; i < 100; i++) {
        const double c = u(g);
        nest.push_back(nested(box(c - 5, c - 5, c - 5, c + 5, c + 5, c + 5), (i % 2) ? box(c - 1, c - 2, c - 1.5, c + 1.25, c + 2, c + 1) : uvSphere(4 + i % 6, 5 + i % 7, 2.5, c, c, c)));
    }
    std::uniform_int_distribution<int> lvl(0, 3);
    for (int i = 0; i < 1000; i++) rnd.push_back(randomClosed(g, lvl(g)));
    for (int i = 0; i < 200; i++) small.push_back(tetra(g));
    checkMeshes(L, "boxes (240)", boxes, &cs);
    checkMeshes(L, "uv spheres (100, 1/3 centred at 1e8)", spheres, &cs);
    checkMeshes(L, "tori (100, half centred at 1e7)", tori, &cs);
    checkMeshes(L, "nested cavities (100)", nest, &cs);
    checkMeshes(L, "random closed meshes (1000, wide exponents, 1e8 offsets)", rnd, &cs);
    checkMeshes(L, "random tetrahedra (200)", small, &cs);
    // analytic cross-check for boxes (independent of the oracle): V = product of side lengths, as exact rationals
    {
        long wrong = 0, total = 0;
        vector<std::array<double, 6>> bx;
        vector<string> docs;
        for (int i = 0; i < 200; i++) {
            const double lx = u(g), ly = u(g), lz = u(g);
            std::array<double, 6> b = {lx, ly, lz, lx + pos(g), ly + pos(g), lz + pos(g)};
            bx.push_back(b);
            docs.push_back(docOf(i, packMesh(box(b[0], b[1], b[2], b[3], b[4], b[5]))));
        }
        auto r = run(L, spec([](bm::Builder& b) { b.i32("batchSize", 4096); }), docs);
        for (int i = 0; i < 200 && r.ok && (size_t)i < r.docs.size(); i++) {
            total++;
            auto& b = bx[i];
            cpp_rational want = (ratOfDouble(b[3]) - ratOfDouble(b[0])) * (ratOfDouble(b[4]) - ratOfDouble(b[1])) * (ratOfDouble(b[5]) - ratOfDouble(b[2]));
            Out o = parseOut(r.docs[i]);
            if (!o.ok || o.exact != ratStr(want)) wrong++;
        }
        if (!r.ok) wrong = 200, total = 200;
        cs.total += total; cs.wrong += wrong;
        result("analytic box volume = side product (200)", wrong == 0, "wrong=" + std::to_string(wrong));
    }
    return cs;
}

// ------------------------------------------------------------------ error documents (bar 3)
void errorDocs(const ExtLib& L) {
    mesh::Mesh good = box(0, 0, 0, 1, 2, 3);
    const string goodPacked = packMesh(good);
    struct Case { string name; string doc; int expect; };
    vector<Case> cs;
    auto withBin = [&](const string& packed, uint8_t subtype = 0) {
        bm::Builder b; b.i32("_id", 0); b.head(bm::kBinData, "mesh"); int32_t l = (int32_t)packed.size(); b.raw(&l, 4); b.b.push_back((char)subtype); b.raw(packed.data(), packed.size()); return b.finish();
    };
    cs.push_back({"missing field", [] { bm::Builder b; b.i32("_id", 0); b.i32("other", 1); return b.finish(); }(), 1});
    cs.push_back({"field is a string", [] { bm::Builder b; b.i32("_id", 0); b.str("mesh", "not a mesh"); return b.finish(); }(), 2});
    cs.push_back({"field is an int", [] { bm::Builder b; b.i32("_id", 0); b.i32("mesh", 5); return b.finish(); }(), 2});
    cs.push_back({"BinData subtype 128", withBin(goodPacked, 128), 3});
    cs.push_back({"BinData shorter than header", withBin(goodPacked.substr(0, 7)), 4});
    { string p = goodPacked; p[0] = 'X'; cs.push_back({"bad magic", withBin(p), 4}); }
    cs.push_back({"payload shorter than header claims", withBin(goodPacked.substr(0, goodPacked.size() - 4)), 5});
    cs.push_back({"payload longer than header claims", withBin(goodPacked + "xxxx"), 5});
    { mesh::Mesh m = good; m.t[5] = 99; cs.push_back({"triangle index out of range", withBin(packMesh(m)), 6}); }
    { mesh::Mesh m = good; m.v[4] = std::nan(""); cs.push_back({"NaN coordinate", withBin(packMesh(m)), 7}); }
    { mesh::Mesh m = good; m.v[7] = INFINITY; cs.push_back({"Inf coordinate", withBin(packMesh(m)), 7}); }
    { mesh::Mesh m = good; m.t.resize(m.t.size() - 3); cs.push_back({"open mesh (one triangle removed)", withBin(packMesh(m)), 8}); }
    { mesh::Mesh m = good; m.addT(m.t[0], m.t[1], m.t[2]); cs.push_back({"duplicate triangle (edge used twice)", withBin(packMesh(m)), 9}); }
    { mesh::Mesh m = good; m.t[1] = m.t[0]; cs.push_back({"degenerate triangle", withBin(packMesh(m)), 9}); }
    { mesh::Mesh m; m.addV(0, 0, 0); cs.push_back({"empty mesh (0 triangles)", withBin(packMesh(m)), 14}); }
    { string p; uint32_t h[3] = {mesh::kMagicMesh, 0xFFFFFFFFu, 0xFFFFFFFFu}; p.append(reinterpret_cast<char*>(h), 12); cs.push_back({"counts overflow 32-bit size math", withBin(p), 10}); }
    { string d = withBin(goodPacked); d.resize(d.size() - 9); cs.push_back({"truncated BSON document", d, 13}); }
    { string d = withBin(goodPacked); d[d.size() - 1] = 1; cs.push_back({"BSON missing terminator", d, 13}); }
    // all error cases in ONE batch together with valid meshes: errors must not affect their neighbours
    vector<string> docs;
    for (size_t i = 0; i < cs.size(); i++) { docs.push_back(cs[i].doc); docs.push_back(docOf((int)(1000 + i), goodPacked)); }
    auto r = run(L, spec([](bm::Builder& b) { b.i32("batchSize", 4096); }), docs);
    result("error docs: pipeline completes OK (no crash, status OK)", r.ok && r.docs.size() == docs.size(), r.ok ? "" : r.err.reason);
    if (!r.ok) return;
    long bad = 0;
    for (size_t i = 0; i < cs.size(); i++) {
        Out e = parseOut(r.docs[2 * i]);
        Out v = parseOut(r.docs[2 * i + 1]);
        const bool eOk = e.hasErr && e.errCode == cs[i].expect;
        const bool vOk = v.ok && v.exact == "6";
        result("error doc: " + cs[i].name, eOk && vOk, "got code=" + std::to_string(e.errCode) + " expected=" + std::to_string(cs[i].expect) + (vOk ? ", neighbour mesh OK" : ", NEIGHBOUR WRONG"));
        if (!(eOk && vOk)) bad++;
    }
}

// ------------------------------------------------------------------ bar 1
void bar1(const string& dir, ExtLib* good) {
    *good = loadExtension(dir + "/libmeshvol_ext.dylib");
    string vs;
    for (auto& v : good->advertised) vs += std::to_string(v.major) + "." + std::to_string(v.minor) + " ";
    result("bar1 good extension loads", good->ok, "advertised=[" + vs + "] negotiated=" + std::to_string(good->negotiated.major) + "." + std::to_string(good->negotiated.minor) + " registered=" + good->stageName + (good->ok ? "" : " error=" + good->error));
    result("bar1 reports API 1.0 and stage $meshVolume", good->ok && good->negotiated.major == 1 && good->negotiated.minor == 0 && good->ext->version.major == 1 && good->stageName == "$meshVolume");
    const char* names[] = {"", "advertises only major 2", "advertises minor 1.1 (> host 1.0)", "advertises an empty version list", "returns extension claiming 2.0 after negotiating 1.0",
                           "lacks get_mongodb_extension symbol", "initialize() returns an error", "advertises major UINT32_MAX"};
    for (int v = 1; v <= 7; v++) {
        ExtLib b = loadExtension(dir + "/libbad_v" + std::to_string(v) + ".dylib");
        result(string("bar1 planted bad extension rejected: ") + names[v], !b.ok && !b.error.empty() && g_escapes == 0, "reason=\"" + b.error + "\"");
    }
    ExtLib m = loadExtension(dir + "/does_not_exist.dylib");
    result("bar1 missing file rejected cleanly", !m.ok, "reason=\"" + m.error.substr(0, 90) + "\"");
}

// ------------------------------------------------------------------ bar 2
void bar2(const ExtLib& L) {
    std::mt19937_64 g(777);
    for (int N : {1, 10, 1000, 100000}) {
        vector<mesh::Mesh> meshes;
        vector<string> docs;
        for (int i = 0; i < N; i++) { meshes.push_back(tetra(g)); docs.push_back(docOf(i, packMesh(meshes.back()))); }
        Counters c1 = counters(L);
        auto r = run(L, spec([&](bm::Builder& b) { b.i32("batchSize", N); }), docs);
        Counters c2 = counters(L);
        long inOrderWrong = 0;
        for (int i = 0; i < N && (size_t)i < r.docs.size(); i++) {
            Out o = parseOut(r.docs[i]);
            if (o.id != i || !o.ok || o.exact != ratStr(oracleVolume(meshes[i]))) inOrderWrong++;
        }
        const long computeCalls = (c2.computeCalls - c1.computeCalls) - 1;  // minus the closing counters probe's own call
        result("bar2 N=" + std::to_string(N) + " one stage invocation, N results in order", r.ok && (int)r.docs.size() == N && inOrderWrong == 0,
               "results=" + std::to_string(r.docs.size()) + " wrong/out-of-order=" + std::to_string(inOrderWrong) + " hostGetNext=" + std::to_string(r.hostGetNext) + " sourcePulls=" + std::to_string(r.sourcePulls) +
                   " computeCalls=" + std::to_string(computeCalls) + " maxBatch=" + std::to_string(c2.maxBatch) + " ms=" + std::to_string((long)r.ms));
        result("bar2 N=" + std::to_string(N) + " ONE batch call received all documents", computeCalls == 1, "computeCalls=" + std::to_string(computeCalls));
    }
    // contrast: same documents, smaller batch sizes
    vector<string> docs;
    for (int i = 0; i < 100000; i++) docs.push_back(docOf(i, packMesh(tetra(g))));
    for (int bs : {1024, 1}) {
        const int N = bs == 1 ? 1000 : 100000;
        vector<string> d(docs.begin(), docs.begin() + N);
        Counters c1 = counters(L);
        auto r = run(L, spec([&](bm::Builder& b) { b.i32("batchSize", bs); }), d);
        Counters c2 = counters(L);
        const long calls = (c2.computeCalls - c1.computeCalls) - 1;
        const long want = (N + bs - 1) / bs;
        result("bar2 contrast N=" + std::to_string(N) + " batchSize=" + std::to_string(bs), r.ok && (int)r.docs.size() == N && calls == want, "computeCalls=" + std::to_string(calls) + " expected ceil(N/batchSize)=" + std::to_string(want));
    }
}

// ------------------------------------------------------------------ bar 4
void bar4(const ExtLib& L) {
    mesh::Mesh m = box(0, 0, 0, 1, 1, 1);
    vector<string> docs = {docOf(0, packMesh(m)), docOf(1, packMesh(m))};
    Counters c1 = counters(L);
    long esc0 = g_escapes;
    for (const char* plant : {"throw", "throwGetNext", "throwParse", "throwOpen"}) {
        auto r = run(L, spec([&](bm::Builder& b) { b.str("plant", plant); }), docs);
        result(string("bar4 planted C++ throw (") + plant + ") returned as a status", !r.ok && r.err.code == -1 && !r.err.escaped && r.err.reason.find("caught C++ exception") != std::string::npos,
               "failedAt=" + r.failedAt + " code=" + std::to_string(r.err.code) + " reason=\"" + r.err.reason + "\"");
    }
    Counters c2 = counters(L);
    result("bar4 no C++ exception escaped to the host", g_escapes == esc0, "escapes=" + std::to_string(g_escapes - esc0) + " extensionCaught=" + std::to_string(c2.throwsCaught - c1.throwsCaught));
    auto r = run(L, spec([](bm::Builder&) {}), docs);
    result("bar4 stage still works after the throws", r.ok && r.docs.size() == 2 && parseOut(r.docs[0]).exact == "1");
}

// ------------------------------------------------------------------ bar 5
int childRun(const ExtLib& L, const string& specDoc, bool catchEscape) {
    mesh::Mesh m = box(0, 0, 0, 1, 1, 1);
    vector<string> docs = {docOf(0, packMesh(m))};
    HostCtx ctx;
    if (!catchEscape) {  // no host-side try/catch around the extension call at all
        // replicate the lifecycle manually up to get_next without take()'s catch
        MongoExtensionAggStageParseNode* parse = nullptr;
        MongoExtensionStatus* s = L.desc->vtable->parse(L.desc, bv(stageBson(specDoc)), &parse);
        (void)s;
        return 0;
    }
    auto r = runPipeline(L, stageBson(specDoc), docs, ctx);
    return r.ok ? 0 : 3;
}
// Runs f in a forked child; returns how the child ended.
string forkRun(const std::function<int()>& f) {
    std::fflush(stdout);
    pid_t p = fork();
    if (p == 0) { int rc = f(); std::fflush(stdout); _exit(rc); }
    int st = 0;
    waitpid(p, &st, 0);
    if (WIFSIGNALED(st)) return "killed by signal " + std::to_string(WTERMSIG(st)) + " (" + strsignal(WTERMSIG(st)) + ")";
    return "exited normally with code " + std::to_string(WEXITSTATUS(st));
}

void bar5(const ExtLib& L, const string& dir) {
    // (a) planted crash: a segfault in the extension takes the host process down. Demonstrated in a forked child so the harness survives.
    string how = forkRun([&] { return childRun(L, spec([](bm::Builder& b) { b.str("plant", "segv"); }), true); });
    result("bar5 planted segfault kills the whole host process (not survivable in-process)", how.find("signal") != string::npos, "child " + how);
    // (b) polite timeout: deadline in 100 ms, stage spins up to 2000 ms but polls deadline and interrupt
    {
        HostCtx ctx; ctx.deadlineMs = nowMs() + 100;
        auto r = runPipeline(L, stageBson(spec([](bm::Builder& b) { b.str("plant", "spinPolite"); b.i32("spinMs", 2000); })), {docOf(0, packMesh(box(0, 0, 0, 1, 1, 1)))}, ctx);
        result("bar5 timeout, cooperative stage stops at the deadline with a status", !r.ok && r.err.code == 50 && r.ms < 500, "code=" + std::to_string(r.err.code) + " elapsedMs=" + std::to_string((long)r.ms) + " (deadline 100 ms; spin would have run 2000 ms) reason=\"" + r.err.reason + "\"");
    }
    // (c) killOp: no deadline, a watchdog thread raises the kill flag at 80 ms; the stage notices through check_for_interrupt
    {
        HostCtx ctx;
        std::thread w([&] { std::this_thread::sleep_for(std::chrono::milliseconds(80)); ctx.killed = true; });
        auto r = runPipeline(L, stageBson(spec([](bm::Builder& b) { b.str("plant", "spinPolite"); b.i32("spinMs", 2000); })), {docOf(0, packMesh(box(0, 0, 0, 1, 1, 1)))}, ctx);
        w.join();
        result("bar5 killOp reaches a cooperative stage via check_for_interrupt", !r.ok && r.err.code == 11601 && r.ms < 500, "code=" + std::to_string(r.err.code) + " elapsedMs=" + std::to_string((long)r.ms) + " interruptChecks=" + std::to_string(ctx.interruptChecks.load()));
    }
    // (d) rude timeout: deadline 100 ms, kill raised at 100 ms, the stage busy-loops 600 ms without polling
    {
        HostCtx ctx; ctx.deadlineMs = nowMs() + 100;
        std::thread w([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); ctx.killed = true; });
        auto r = runPipeline(L, stageBson(spec([](bm::Builder& b) { b.str("plant", "spinRude"); b.i32("spinMs", 600); })), {docOf(0, packMesh(box(0, 0, 0, 1, 1, 1)))}, ctx);
        w.join();
        result("bar5 timeout, non-polling stage overruns the deadline; host cannot preempt it", (r.ok || r.err.code == 50) && r.ms >= 550 && ctx.interruptChecks == 0,
               string("pipeline ended after ") + std::to_string((long)r.ms) + " ms against a 100 ms deadline (" + (r.ok ? "OK" : "error " + std::to_string(r.err.code) + " raised only at the stage's next checkpoint, after the spin") +
                   "); kill flag was set at 100 ms; extension called check_for_interrupt " + std::to_string(ctx.interruptChecks.load()) + " times during the overrun");
    }
    // (e) what an escaping C++ exception does (the broken variant, NOT the shipped one): host with no try/catch around the call
    ExtLib nc = loadExtension(dir + "/libmeshvol_nocatch.dylib");
    if (nc.ok) {
        string h1 = forkRun([&] {
            HostCtx ctx;
            string st = stageBson(spec([](bm::Builder& b) { b.str("plant", "throwParse"); }));
            MongoExtensionAggStageParseNode* pn = nullptr;
            nc.desc->vtable->parse(nc.desc, bv(st), &pn);  // no catch: the planted exception unwinds through the host frame
            return 0;
        });
        string h2 = forkRun([&] { return childRun(nc, spec([](bm::Builder& b) { b.str("plant", "throwParse"); }), true); });
        result("bar5 info: exception escaping the C boundary (build without the boundary catch)", true, "host without try/catch: child " + h1 + "; host with try/catch(...) (this toolchain only, contract says undefined): child " + h2);
    }
}

// ------------------------------------------------------------------ cleanup accounting
void finalAccounting(const ExtLib& L) {
    Counters c = counters(L);
    result("accounting: every returned ByteBuf was destroyed by the host", c.ok && c.liveBufs == 0, "liveBufs=" + std::to_string(c.liveBufs));
    result("accounting: every heap status was destroyed", c.ok && c.liveStatuses == 0, "liveStatuses=" + std::to_string(c.liveStatuses));
    result("accounting: no stage objects leaked (only the probe pipeline's own exec stage is alive)", c.ok && c.liveObjs == 1, "liveObjs=" + std::to_string(c.liveObjs));
}
}  // namespace


// ------------------------------------------------------------------ MP14d: $meshContains plumbing tests (the verdicts themselves are checked against the exactmesh oracle in test_contains_core)
string pointsArray(const vector<double>& pts) {
    bm::Builder arr;
    for (size_t i = 0; i < pts.size() / 3; i++) {
        bm::Builder one; one.dbl("0", pts[3*i]).dbl("1", pts[3*i+1]).dbl("2", pts[3*i+2]);
        arr.head(bm::kArray, std::to_string(i)); arr.b.append(one.finish());
    }
    return arr.finish();
}
string containsDocOf(int id, const string& packed, const string* pointsArr, const string* pointsBin) {
    bm::Builder b; b.i32("_id", id); b.bin("mesh", packed.data(), packed.size());
    if (pointsArr) { b.head(bm::kArray, "points"); b.b.append(*pointsArr); }
    if (pointsBin) b.bin("points", pointsBin->data(), pointsBin->size());
    return b.finish();
}
string outStr(const string& d, const char* field, int* errCode) {
    bm::Elem e; *errCode = 0;
    auto* p = reinterpret_cast<const uint8_t*>(d.data());
    if (!bm::findField(p, d.size(), field, &e)) { *errCode = -1; return ""; }
    if (e.type == bm::kString) return string(reinterpret_cast<const char*>(e.val + 4), e.vlen - 5);
    bm::Elem er;
    if (e.type == bm::kDoc && bm::findField(e.val, e.vlen, "error", &er) && er.type == bm::kDoc) { bm::Elem c; if (bm::findField(er.val, er.vlen, "code", &c) && c.type == bm::kInt32) std::memcpy(errCode, c.val, 4); }
    return "";
}
int runContainsTests(const string& dir) {
    ExtLib L = loadExtension(dir);
    if (!L.ok) { std::printf("load failed: %s\n", L.error.c_str()); return 2; }
    int fails = 0;
    auto chk = [&](const string& name, bool ok, const string& extra = "") { std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", name.c_str(), extra.c_str()); if (!ok) fails++; };
    chk("both stages registered, names", L.stageName == "$meshVolume" && L.descContains && sv(L.descContains->vtable->get_name(L.descContains)) == "$meshContains");
    mesh::Mesh bx = box(0, 0, 0, 1, 1, 1), sp = uvSphere(10, 14, 1.0, 0.25, 0.0, -0.125);
    auto ref = [&](const mesh::Mesh& m, const vector<double>& pts) {
        string v; for (size_t i = 0; i < pts.size() / 3; i++) { auto k = meshcontains::classify(m.v, m.t, &pts[3*i]); v += k == meshcontains::kIn ? 'I' : k == meshcontains::kOn ? 'B' : 'O'; } return v; };
    vector<double> boxPts = {0.5,0.5,0.5, 2,0.5,0.5, -1,0.5,0.5, 0,0,0, 1,1,1, 0.5,0,0.5, 0,0.5,0.5, 0.5,0.5,1, 0.5,0.5,std::nextafter(1.0, 2.0), 0.5,0.5,std::nextafter(1.0, 0.0), 1,0.5,0.5, 0.25,1,0.75};
    vector<double> spPts; { std::mt19937_64 g(7); for (int i = 0; i < 300; i++) { spPts.push_back((double)(g() >> 11) / 9007199254740992.0 * 3 - 1.25); spPts.push_back((double)(g() >> 11) / 9007199254740992.0 * 3 - 1.5); spPts.push_back((double)(g() >> 11) / 9007199254740992.0 * 3 - 1.5); } }
    string bxArr = pointsArray(boxPts), spArr = pointsArray(spPts);
    string spBin(reinterpret_cast<const char*>(spPts.data()), spPts.size() * 8);
    vector<string> docs = {containsDocOf(1, packMesh(bx), &bxArr, nullptr), containsDocOf(2, packMesh(sp), &spArr, nullptr), containsDocOf(3, packMesh(sp), nullptr, &spBin)};
    auto r = runContains(L, spec([](bm::Builder&) {}), docs);
    chk("pipeline runs, 3 docs in order", r.ok && r.docs.size() == 3);
    if (r.ok && r.docs.size() == 3) {
        int ec; string v0 = outStr(r.docs[0], "contains", &ec), v1 = outStr(r.docs[1], "contains", &ec), v2 = outStr(r.docs[2], "contains", &ec);
        chk("box verdicts (incl. vertices, faces, ulp neighbours) equal the core", v0 == ref(bx, boxPts), v0);
        chk("sphere verdicts, BSON array points, equal the core", v1 == ref(sp, spPts));
        chk("sphere verdicts, BinData points, equal the core", v2 == ref(sp, spPts));
        chk("all three verdict classes occur", v1.find('I') != string::npos && v1.find('O') != string::npos && v0.find('B') != string::npos);
        chk("input fields preserved, _id", [&] { bm::Elem e; auto* p = reinterpret_cast<const uint8_t*>(r.docs[0].data()); return bm::findField(p, r.docs[0].size(), "_id", &e) && e.type == bm::kInt32; }());
    }
    // errors are per document
    string notArr = pointsArray({0.5, 0.5, 0.5}); mesh::Mesh open = bx; open.t.resize(open.t.size() - 3);
    bm::Builder bad1; bad1.i32("_id", 10); bad1.bin("mesh", packMesh(bx).data(), packMesh(bx).size());                                          // missing points
    bm::Builder bad2; bad2.i32("_id", 11); bad2.bin("mesh", packMesh(bx).data(), packMesh(bx).size()); bad2.str("points", "nope");             // points not an array
    string shortBin(24 * 2 + 8, 'x');
    bm::Builder bad3; bad3.i32("_id", 12); bad3.bin("mesh", packMesh(bx).data(), packMesh(bx).size()); bad3.bin("points", shortBin.data(), shortBin.size());  // bin length not a multiple of 24
    string badPt; { bm::Builder one; one.dbl("0", 1).dbl("1", 2); bm::Builder arr; arr.head(bm::kArray, "0"); arr.b.append(one.finish()); badPt = arr.finish(); }
    string okArr = pointsArray({0.5, 0.5, 0.5});
    vector<string> errDocs = {bad1.finish(), bad2.finish(), bad3.finish(), containsDocOf(13, packMesh(bx), nullptr, nullptr),
        [&] { bm::Builder b; b.i32("_id", 14); b.bin("mesh", packMesh(bx).data(), packMesh(bx).size()); b.head(bm::kArray, "points"); b.b.append(badPt); return b.finish(); }(),
        containsDocOf(15, packMesh(open), &okArr, nullptr), containsDocOf(16, string("junk"), &okArr, nullptr), containsDocOf(17, packMesh(bx), &okArr, nullptr)};
    auto re = runContains(L, spec([](bm::Builder&) {}), errDocs);
    chk("error pipeline runs", re.ok && re.docs.size() == errDocs.size());
    if (re.ok && re.docs.size() == errDocs.size()) {
        const int want[] = {1, 15, 15, 1, 15, 8, 4, 0}; bool all = true; string got;
        for (size_t i = 0; i < errDocs.size(); i++) { int ec; string v = outStr(re.docs[i], "contains", &ec); got += std::to_string(ec) + " "; if (i < 7 ? ec != want[i] : v != "I") all = false; }
        chk("per-document errors: missing points 1, not array 15, bad bin 15, no points field 1, bad point 15, open mesh 8, junk 4, good doc ok", all, got);
    }
    // batching: 200 docs, one computeBatch
    vector<string> many; string onePt = pointsArray({0.5, 0.5, 0.5}); for (int i = 0; i < 200; i++) many.push_back(containsDocOf(i, packMesh(bx), &onePt, nullptr));
    auto rb = runContains(L, spec([](bm::Builder& b) { b.i32("batchSize", 1000); }), many);
    chk("200 documents, 200 results in order", rb.ok && rb.docs.size() == 200);
    // input contract (review of 2026-11-16): coordinate types, NaN/Inf, empty arrays, -0.0
    {
        auto docWithPoints = [&](int id, const std::function<void(bm::Builder&)>& pts) { bm::Builder b; b.i32("_id", id); b.bin("mesh", packMesh(bx).data(), packMesh(bx).size()); b.head(bm::kArray, "points"); bm::Builder a; pts(a); b.b.append(a.finish()); return b.finish(); };
        auto point = [](bm::Builder& a, const char* idx, const std::function<void(bm::Builder&)>& coords) { bm::Builder one; coords(one); a.head(bm::kArray, idx); a.b.append(one.finish()); };
        vector<string> cd = {
            docWithPoints(30, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.i32("0", 0).i32("1", 0).i32("2", 0); }); }),                                       // int32 vertex of the box: ON
            docWithPoints(31, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.i64("0", 1).i64("1", 1).i64("2", 1); }); }),                                       // int64 small: ON (corner)
            docWithPoints(32, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.i64("0", (1LL << 53) + 1).i64("1", 0).i64("2", 0); }); }),                       // int64 not representable: error 15
            docWithPoints(33, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.dbl("0", std::nan("")).dbl("1", 0).dbl("2", 0); }); }),                          // NaN: error 15
            docWithPoints(34, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.dbl("0", INFINITY).dbl("1", 0).dbl("2", 0); }); }),                              // Inf: error 15
            docWithPoints(35, [&](bm::Builder& a) { (void)a; }),                                                                                                         // empty points array: valid, empty verdict string
            docWithPoints(36, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.dbl("0", -0.0).dbl("1", 0.5).dbl("2", 0.5); }); }),                             // -0.0 on the x=0 face: ON
            docWithPoints(37, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.dbl("0", 0.5).dbl("1", 0.5).dbl("2", 0.5).dbl("3", 0.5); }); }),               // 4 coordinates: error 15
            docWithPoints(38, [&](bm::Builder& a) { point(a, "0", [](bm::Builder& o) { o.str("0", "x").dbl("1", 0).dbl("2", 0); }); }),                                   // string coordinate: error 15
        };
        string nanBin(24, 0); { double nn = std::nan(""); std::memcpy(&nanBin[0], &nn, 8); }
        cd.push_back(containsDocOf(39, packMesh(bx), nullptr, &nanBin));                                                                                                // NaN in BinData: error 7
        auto rc = runContains(L, spec([](bm::Builder&) {}), cd);
        chk("input contract: pipeline runs", rc.ok && rc.docs.size() == cd.size());
        if (rc.ok && rc.docs.size() == cd.size()) {
            vector<string> got; vector<int> ecs;
            for (auto& d : rc.docs) { int ec; got.push_back(outStr(d, "contains", &ec)); ecs.push_back(ec); }
            chk("int32 coordinates accepted (vertex: B)", ecs[0] == 0 && got[0] == "B", got[0]);
            chk("int64 coordinates accepted (corner: B)", ecs[1] == 0 && got[1] == "B", got[1]);
            chk("int64 beyond 2^53 rejected (15)", ecs[2] == 15);
            chk("NaN as double rejected (15)", ecs[3] == 15);
            chk("Inf as double rejected (15)", ecs[4] == 15);
            chk("empty points array gives an empty verdict string", ecs[5] == 0 && got[5].empty());
            chk("-0.0 coordinate on the x=0 face: B", ecs[6] == 0 && got[6] == "B", got[6]);
            chk("4 coordinates rejected (15)", ecs[7] == 15);
            chk("string coordinate rejected (15)", ecs[8] == 15);
            chk("NaN in BinData points rejected (7)", ecs[9] == 7);
        }
    }
    // interrupt polling inside one big document: 600k points against a 20k-triangle sphere with a deadline 150 ms away must stop with the deadline status long before the work is done
    {
        mesh::Mesh big = uvSphere(100, 200, 1.0, 0, 0, 0); vector<double> many; std::mt19937_64 g(5);
        for (int i = 0; i < 600000; i++) { many.push_back((double)(g() >> 11) / 9007199254740992.0 * 3 - 1.5); many.push_back((double)(g() >> 11) / 9007199254740992.0 * 3 - 1.5); many.push_back((double)(g() >> 11) / 9007199254740992.0 * 3 - 1.5); }
        string manyBin(reinterpret_cast<const char*>(many.data()), many.size() * 8);
        HostCtx ctx; ctx.deadlineMs = nowMs() + 150;
        auto rd = runPipeline(L, stageBsonContains(spec([](bm::Builder&) {})), {containsDocOf(1, packMesh(big), nullptr, &manyBin)}, ctx, L.descContains);
        chk("deadline inside one document: stops with status 50 after the deadline, not after the whole batch", !rd.ok && rd.err.code == 50 && rd.ms < 2500, "elapsedMs=" + std::to_string((long)rd.ms) + " code=" + std::to_string(rd.err.code));
    }
    // stage options and name mismatch
    auto rs = runContains(L, spec([](bm::Builder& b) { b.str("points", "pts"); b.str("output", "verdict"); }), {[&] { bm::Builder b; b.i32("_id", 1); b.bin("mesh", packMesh(bx).data(), packMesh(bx).size()); b.head(bm::kArray, "pts"); b.b.append(onePt); return b.finish(); }()});
    { int ec; chk("custom points and output field names", rs.ok && rs.docs.size() == 1 && outStr(rs.docs[0], "verdict", &ec) == "I"); }
    auto ru = runContains(L, spec([](bm::Builder& b) { b.str("bogus", "x"); }), many);
    chk("unknown field rejected at parse", !ru.ok);
    // $meshVolume still works
    auto rv = run(L, spec([](bm::Builder&) {}), {docOf(1, packMesh(bx))});
    chk("$meshVolume unaffected", rv.ok && rv.docs.size() == 1 && parseOut(rv.docs[0]).exact == "1");
    std::printf("FAILS %d\n", fails);
    return fails ? 1 : 0;
}

// MP14d step 2: the device path through the stage. Usage: fakehost containsgpu <lib> <case> [npoints]. Loads with `gpu: true`; cases: ok (device path must be used, results equal the CPU core),
// small (below the threshold: CPU), cpu (engine "cpu" knob: CPU), fail (planted device failure: fall back, same results, device disabled afterwards), wrong (planted wrong verdicts: shadow check catches,
// same results, device disabled). Prints counters from the config probe.
int runContainsGpu(const string& dir, const string& which, size_t npts) {
    g_extOptions = "gpu: true";
    ExtLib L = loadExtension(dir);
    if (!L.ok) { std::printf("load failed: %s\n", L.error.c_str()); return 2; }
    mesh::Mesh sp = uvSphere(60, 80, 1.0, 0.25, 0.0, -0.125);   // 9,600 triangles
    vector<double> pts; { std::mt19937_64 g(99); for (size_t i = 0; i < npts * 3; i++) pts.push_back((double)(g() >> 11) / 9007199254740992.0 * 3 - 1.5); }
    // include boundary-ish points: mesh vertices and exact face points, so the device sees UNCERTAIN and the exact path decides
    for (size_t i = 0; i < 40 && i < sp.nv(); i++) { pts.push_back(sp.v[3*i]); pts.push_back(sp.v[3*i+1]); pts.push_back(sp.v[3*i+2]); }
    string bin(reinterpret_cast<const char*>(pts.data()), pts.size() * 8);
    string ref; for (size_t i = 0; i < pts.size() / 3; i++) { auto k = meshcontains::classify(sp.v, sp.t, &pts[3*i]); ref += k == meshcontains::kIn ? 'I' : k == meshcontains::kOn ? 'B' : 'O'; }
    auto sp2 = spec([&](bm::Builder& b) {
        if (which == "ok" || which == "fail" || which == "wrong") b.i32("gpuMinPoints", 1);
        if (which == "cpu") { b.i32("gpuMinPoints", 1); b.str("engine", "cpu"); }
        if (which == "fail") b.str("plant", "gpuFail");
        if (which == "wrong") b.str("plant", "gpuWrong");
    });
    auto probe = [&] { auto q = run(L, spec([](bm::Builder& b) { b.str("probe", "config"); }), {docOf(0, "")}); string line; if (q.ok && !q.docs.empty()) {
        bm::Elem e; auto* p = reinterpret_cast<const uint8_t*>(q.docs[0].data()); bm::findField(p, q.docs[0].size(), "meshVolume", &e);
        const uint8_t* x = e.val + 4; const uint8_t* end = e.val + e.vlen - 1;
        while (x < end) { bm::Elem f; if (!bm::readElem(x, end, &f)) break; x = f.end; if (f.type == bm::kInt64) { int64_t v; std::memcpy(&v, f.val, 8); line += " " + string(f.name) + "=" + std::to_string(v); } else if (f.type == bm::kBool) line += " " + string(f.name) + "=" + (f.val[0] ? "true" : "false"); else if (f.type == bm::kString) line += " " + string(f.name) + "=\"" + string(reinterpret_cast<const char*>(f.val + 4)) + "\""; } }
        return line; };
    if (which == "concurrent" || which == "repeat") {   // several aggregations at once on one device context; or many runs for a leak check
        auto spc = spec([](bm::Builder& b) { b.i32("gpuMinPoints", 1); });
        const int threads = which == "concurrent" ? 6 : 1, runsEach = which == "concurrent" ? 6 : (getenv("MP14D_REPEAT") ? atoi(getenv("MP14D_REPEAT")) : 150);
        std::atomic<int> bad{0}, done{0};
        auto rss = [] { struct rusage ru; getrusage(RUSAGE_SELF, &ru); return (long)ru.ru_maxrss; };   // bytes on macOS, KiB on Linux
        const long rss0 = rss();
        std::vector<std::thread> th;
        for (int t = 0; t < threads; t++) th.emplace_back([&, t] { for (int k = 0; k < runsEach; k++) {
            auto rr = runContains(L, spc, {containsDocOf(t * 1000 + k, packMesh(sp), nullptr, &bin)});
            int e2 = 0; string g2 = rr.ok && rr.docs.size() == 1 ? outStr(rr.docs[0], "contains", &e2) : "";
            if (g2 != ref) bad++; done++; } });
        for (auto& x : th) x.join();
        const long rss1 = rss();
        std::printf("CASE %s threads=%d runs=%d verdict-mismatching runs=%d rssGrowth=%ld (maxrss units)\n", which.c_str(), threads, done.load(), bad.load(), rss1 - rss0);
        std::printf("COUNTERS%s\n", probe().c_str());
        return bad == 0 ? 0 : 1;
    }
    auto t0 = std::chrono::steady_clock::now();
    auto r = runContains(L, sp2, {containsDocOf(1, packMesh(sp), nullptr, &bin)});
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    int ec = 0; string got = r.ok && r.docs.size() == 1 ? outStr(r.docs[0], "contains", &ec) : "";
    std::printf("CASE %s points=%zu stage ok=%d elapsedMs=%.1f verdicts equal CPU core: %s\n", which.c_str(), pts.size() / 3, r.ok ? 1 : 0, ms, got == ref ? "YES" : "NO");
    std::printf("COUNTERS%s\n", probe().c_str());
    // second run after the first: after a planted failure the device must stay disabled and results must still be right
    auto r2 = runContains(L, sp2, {containsDocOf(2, packMesh(sp), nullptr, &bin)});
    int ec2 = 0; string got2 = r2.ok && r2.docs.size() == 1 ? outStr(r2.docs[0], "contains", &ec2) : "";
    std::printf("SECOND RUN verdicts equal CPU core: %s\nCOUNTERS%s\n", got2 == ref ? "YES" : "NO", probe().c_str());
    return got == ref && got2 == ref ? 0 : 1;
}

int main(int argc, char** argv) {
    const string mode = argc > 1 ? argv[1] : "all";
    const string dir = argc > 2 ? argv[2] : ".";
    if (mode == "all") {
        ExtLib good;
        bar1(dir, &good);
        if (!good.ok) { std::printf("cannot continue: good extension did not load\n"); return 2; }
        std::printf("stage properties BSON returned by the extension: %s\n", run(good, spec([](bm::Builder&) {}), {docOf(0, packMesh(box(0, 0, 0, 1, 1, 1)))}).ok ? "pipeline OK" : "pipeline FAILED");
        bar2(good);
        CorpusStats cs = runCorpus(good);
        std::printf("CORPUS total=%ld wrong=%ld exactMismatch=%ld roundingWrong=%ld errors=%ld\n", cs.total, cs.wrong, cs.wrongExact, cs.wrongRound, cs.errors);
        errorDocs(good);
        bar4(good);
        bar5(good, dir);
        finalAccounting(good);
    } else if (mode == "corpus") {  // bar 6: any extension dylib
        ExtLib L = loadExtension(dir);
        if (!L.ok) { std::printf("load failed: %s\n", L.error.c_str()); return 2; }
        CorpusStats cs = runCorpus(L);
        std::printf("CORPUS total=%ld wrong=%ld exactMismatch=%ld roundingWrong=%ld errors=%ld\n", cs.total, cs.wrong, cs.wrongExact, cs.wrongRound, cs.errors);
        std::printf("MUTANT-SUMMARY lib=%s wrong=%ld\n", dir.c_str(), cs.wrong);
        return 0;
    } else if (mode == "containsgpu") {
        return runContainsGpu(dir, argc > 3 ? argv[3] : "ok", argc > 4 ? (size_t)atol(argv[4]) : (argc > 3 && string(argv[3]) == "small" ? 1000 : 100000));
    } else if (mode == "contains") {
        return runContainsTests(dir);
    } else if (mode == "options") {  // MP14c: options <lib> <yaml text>: load with that extensionOptions text, report init result, config probe, corpus
        g_extOptions = argc > 3 ? argv[3] : "";
        ExtLib L = loadExtension(dir);
        std::printf("OPTIONS-LOAD ok=%d error=\"%s\"\n", L.ok ? 1 : 0, L.error.c_str());
        std::printf("VULKAN-IMAGES %d\n", vulkanImages());
        if (!L.ok) return 0;
        auto r = run(L, spec([&](bm::Builder& b) { b.str("probe", "config"); }), {docOf(0, "")});
        if (!r.ok || r.docs.empty()) { std::printf("probe failed: %s\n", r.err.reason.c_str()); return 2; }
        bm::Elem e; auto* p = reinterpret_cast<const uint8_t*>(r.docs[0].data());
        bm::findField(p, r.docs[0].size(), "meshVolume", &e);
        const uint8_t* q = e.val + 4; const uint8_t* end = e.val + e.vlen - 1;
        std::printf("PROBE config:");
        while (q < end) {
            bm::Elem f; if (!bm::readElem(q, end, &f)) break; q = f.end;
            if (f.type == bm::kInt64) { int64_t v; std::memcpy(&v, f.val, 8); std::printf(" %.*s=%lld", (int)f.name.size(), f.name.data(), (long long)v); }
            else if (f.type == bm::kBool) std::printf(" %.*s=%s", (int)f.name.size(), f.name.data(), f.val[0] ? "true" : "false");
            else if (f.type == bm::kString) std::printf(" %.*s=\"%s\"", (int)f.name.size(), f.name.data(), f.val + 4);
        }
        std::printf("\n");
        CorpusStats cs = runCorpus(L);
        std::printf("CORPUS total=%ld wrong=%ld exactMismatch=%ld roundingWrong=%ld errors=%ld\n", cs.total, cs.wrong, cs.wrongExact, cs.wrongRound, cs.errors);
        return 0;
    } else if (mode == "symbol" || mode == "vulkan") {
        ExtLib L = loadExtension(dir);
        if (!L.ok) { std::printf("load failed: %s\n", L.error.c_str()); return 2; }
        auto r = run(L, spec([&](bm::Builder& b) { b.str("probe", mode); }), {docOf(0, "")});
        if (!r.ok || r.docs.empty()) { std::printf("probe failed: %s\n", r.err.reason.c_str()); return 2; }
        bm::Elem e; auto* p = reinterpret_cast<const uint8_t*>(r.docs[0].data());
        bm::findField(p, r.docs[0].size(), "meshVolume", &e);
        const uint8_t* q = e.val + 4; const uint8_t* end = e.val + e.vlen - 1;
        std::printf("PROBE %s:", mode.c_str());
        while (q < end) {
            bm::Elem f; if (!bm::readElem(q, end, &f)) break; q = f.end;
            if (f.type == bm::kInt32) { int32_t v; std::memcpy(&v, f.val, 4); std::printf(" %.*s=%d", (int)f.name.size(), f.name.data(), v); }
            else if (f.type == bm::kInt64) { int64_t v; std::memcpy(&v, f.val, 8); std::printf(" %.*s=%lld", (int)f.name.size(), f.name.data(), (long long)v); }
            else if (f.type == bm::kBool) std::printf(" %.*s=%s", (int)f.name.size(), f.name.data(), f.val[0] ? "true" : "false");
            else if (f.type == bm::kString) std::printf(" %.*s=\"%s\"", (int)f.name.size(), f.name.data(), f.val + 4);
        }
        std::printf("\n");
        return 0;
    }
    std::printf("SUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
