// MP14a fake host. Implements just enough of the HOST side of the Extensions C API (public/api.h only, no server
// code) to dlopen mesh_extension, negotiate, register, parse, promote, compile and drive a stage with N documents.
// Usage: fake_host <path/to/mesh_extension.dylib> <path/to/libmeshhelper.dylib>
#include "mongo/db/extension/public/api.h"

#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "mini_bson.h"

using Clock = std::chrono::steady_clock;
using Bytes = std::vector<uint8_t>;

static int gFail = 0, gPass = 0;
#define CHECK(cond, ...)                                                           \
    do {                                                                           \
        if (cond) { ++gPass; printf("  [PASS] "); }                                \
        else { ++gFail; printf("  [FAIL] (%s:%d) ", __FILE__, __LINE__); }         \
        printf(__VA_ARGS__); printf("\n");                                         \
    } while (0)

static MongoExtensionByteView view(const void* p, size_t n) { return {(const uint8_t*)p, n}; }
static MongoExtensionByteView view(const std::string& s) { return view(s.data(), s.size()); }
static MongoExtensionByteView view(const Bytes& b) { return view(b.data(), b.size()); }
static std::string toStr(MongoExtensionByteView v) { return std::string((const char*)v.data, v.len); }
static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------ vtable call recorder (which entries the harness exercised)
static std::map<std::string, int> gCalls;
#define CALL(T, obj, slot, ...) (gCalls[T "." #slot]++, (obj)->vtable->slot((obj)__VA_OPT__(, ) __VA_ARGS__))

// ------------------------------------------------------------ host-allocated Status / ByteBuf (counted)
static std::atomic<long> hAllocs{0}, hFrees{0};
struct HStatus : MongoExtensionStatus {
    int32_t code; std::string reason;
    HStatus(int32_t c, std::string r) : MongoExtensionStatus{&kVT}, code(c), reason(std::move(r)) { hAllocs++; }
    static void destroy(MongoExtensionStatus* s) noexcept { delete static_cast<HStatus*>(s); hFrees++; }
    static int32_t getCode(const MongoExtensionStatus* s) noexcept { return static_cast<const HStatus*>(s)->code; }
    static MongoExtensionByteView getReason(const MongoExtensionStatus* s) noexcept { return view(static_cast<const HStatus*>(s)->reason); }
    static void setCode(MongoExtensionStatus* s, int32_t c) noexcept { static_cast<HStatus*>(s)->code = c; }
    static MongoExtensionStatus* setReason(MongoExtensionStatus* s, MongoExtensionByteView r) noexcept;
    static MongoExtensionStatus* clone(const MongoExtensionStatus* s, MongoExtensionStatus** out) noexcept;
    static const MongoExtensionStatusVTable kVT;
};
struct HOk : MongoExtensionStatus {
    HOk() : MongoExtensionStatus{&kVT} {}
    static void destroy(MongoExtensionStatus*) noexcept {}
    static int32_t getCode(const MongoExtensionStatus*) noexcept { return 0; }
    static MongoExtensionByteView getReason(const MongoExtensionStatus*) noexcept { return view(nullptr, 0); }
    static void setCode(MongoExtensionStatus*, int32_t) noexcept {}
    static MongoExtensionStatus* setReason(MongoExtensionStatus*, MongoExtensionByteView) noexcept;
    static MongoExtensionStatus* clone(const MongoExtensionStatus*, MongoExtensionStatus** o) noexcept;
    static const MongoExtensionStatusVTable kVT;
};
static HOk gHOk;
static MongoExtensionStatus* hOk() { return &gHOk; }
static MongoExtensionStatus* hErr(int32_t c, std::string r) { return new HStatus(c, std::move(r)); }
MongoExtensionStatus* HStatus::setReason(MongoExtensionStatus* s, MongoExtensionByteView r) noexcept { static_cast<HStatus*>(s)->reason = toStr(r); return hOk(); }
MongoExtensionStatus* HStatus::clone(const MongoExtensionStatus* s, MongoExtensionStatus** o) noexcept { auto* x = static_cast<const HStatus*>(s); *o = hErr(x->code, x->reason); return hOk(); }
MongoExtensionStatus* HOk::setReason(MongoExtensionStatus*, MongoExtensionByteView) noexcept { return hErr(-1, "forbidden"); }
MongoExtensionStatus* HOk::clone(const MongoExtensionStatus*, MongoExtensionStatus** o) noexcept { *o = &gHOk; return hOk(); }
const MongoExtensionStatusVTable HStatus::kVT{&HStatus::destroy, &HStatus::getCode, &HStatus::getReason, &HStatus::setCode, &HStatus::setReason, &HStatus::clone};
const MongoExtensionStatusVTable HOk::kVT{&HOk::destroy, &HOk::getCode, &HOk::getReason, &HOk::setCode, &HOk::setReason, &HOk::clone};

struct HBuf : MongoExtensionByteBuf {
    Bytes b;
    explicit HBuf(Bytes x) : MongoExtensionByteBuf{&kVT}, b(std::move(x)) { hAllocs++; }
    static void destroy(MongoExtensionByteBuf* p) noexcept { delete static_cast<HBuf*>(p); hFrees++; }
    static MongoExtensionByteView getView(const MongoExtensionByteBuf* p) noexcept { return view(static_cast<const HBuf*>(p)->b); }
    static const MongoExtensionByteBufVTable kVT;
};
const MongoExtensionByteBufVTable HBuf::kVT{&HBuf::destroy, &HBuf::getView};

// Take an extension-allocated status: read code/reason, destroy THROUGH THE EXTENSION'S vtable.
struct St { int code; std::string reason; };
static St consume(MongoExtensionStatus* s) {
    if (!s) return {-999, "NULL status returned (contract violation)"};
    St r{s->vtable->get_code(s), toStr(s->vtable->get_reason(s))};
    s->vtable->destroy(s);
    return r;
}
// Take an extension-allocated ByteBuf: copy and destroy through its vtable.
static Bytes consumeBuf(MongoExtensionByteBuf* b) {
    if (!b) return {};
    auto v = b->vtable->get_view(b); Bytes r(v.data, v.data + v.len);
    b->vtable->destroy(b);
    return r;
}

// ------------------------------------------------------------ query execution context (deadline / killOp)
struct Ctx : MongoExtensionQueryExecutionContext {
    int64_t deadlineMs = INT64_MAX;
    std::atomic<bool> killed{false};
    long interruptChecks = 0;
    Ctx() : MongoExtensionQueryExecutionContext{&kVT} {}
    // what the real host does: OperationContext::checkForInterruptNoAssert() (host/query_execution_context.h:28)
    St interrupted() { ++interruptChecks; if (killed) return {11601, "operation was interrupted"}; if (nowMs() > deadlineMs) return {50, "operation exceeded time limit"}; return {0, ""}; }
    static MongoExtensionStatus* checkForInterrupt(const MongoExtensionQueryExecutionContext* c, MongoExtensionStatus* qs) noexcept {
        St s = const_cast<Ctx*>(static_cast<const Ctx*>(c))->interrupted();
        if (s.code != 0) { qs->vtable->set_code(qs, s.code); auto* r = qs->vtable->set_reason(qs, view(s.reason)); r->vtable->destroy(r); }
        return hOk();
    }
    static MongoExtensionStatus* getMetrics(const MongoExtensionQueryExecutionContext*, MongoExtensionExecAggStage*, MongoExtensionOperationMetrics**) noexcept { return hErr(1, "fake host: get_metrics unsupported"); }
    static MongoExtensionStatus* getDeadline(const MongoExtensionQueryExecutionContext* c, int64_t* o) noexcept { *o = static_cast<const Ctx*>(c)->deadlineMs; return hOk(); }
    static MongoExtensionStatus* getHostMetrics(const MongoExtensionQueryExecutionContext*, const MongoExtensionByteView*, uint64_t, MongoExtensionByteBuf**) noexcept { return hErr(1, "unsupported"); }
    static MongoExtensionStatus* setBatchSize(const MongoExtensionQueryExecutionContext*, uint64_t) noexcept {
        return hErr(13150705, "setBatchSize is not supported outside of a document results and metadata stream");  // real host message
    }
    static const MongoExtensionQueryExecutionContextVTable kVT;
};
const MongoExtensionQueryExecutionContextVTable Ctx::kVT{&Ctx::checkForInterrupt, &Ctx::getMetrics, &Ctx::getDeadline, &Ctx::getHostMetrics, &Ctx::setBatchSize};

struct ShapeOpts : MongoExtensionHostQueryShapeOpts {
    ShapeOpts() : MongoExtensionHostQueryShapeOpts{&kVT} {}
    static MongoExtensionStatus* ident(const MongoExtensionHostQueryShapeOpts*, MongoExtensionByteView v, MongoExtensionByteBuf** o) noexcept { *o = new HBuf(Bytes(v.data, v.data + v.len)); return hOk(); }
    static MongoExtensionStatus* path(const MongoExtensionHostQueryShapeOpts*, MongoExtensionByteView v, MongoExtensionByteBuf** o) noexcept {
        std::string s = "HASH<" + toStr(v) + ">"; *o = new HBuf(Bytes(s.begin(), s.end())); return hOk();
    }
    static MongoExtensionStatus* lit(const MongoExtensionHostQueryShapeOpts*, MongoExtensionByteView, MongoExtensionByteBuf**) noexcept { return hErr(1, "unsupported"); }
    static const MongoExtensionHostQueryShapeOptsVTable kVT;
};
const MongoExtensionHostQueryShapeOptsVTable ShapeOpts::kVT{&ShapeOpts::ident, &ShapeOpts::path, &ShapeOpts::lit};

// ------------------------------------------------------------ host source stage (what set_source hands the extension)
struct Source : MongoExtensionExecAggStage {
    std::vector<Bytes> docs;
    std::string kind = "alt";  // buf | view | alt  (how the HOST returns documents)
    int failAt = -1;           // return a host error status when about to deliver this index
    size_t i = 0;
    long calls = 0;
    Bytes viewStore, metaStore;  // backing for kByteView results: OVERWRITTEN on the next call (the documented lifetime)
    Source() : MongoExtensionExecAggStage{&kVT} {}
    static MongoExtensionStatus* getNext(MongoExtensionExecAggStage* e, MongoExtensionQueryExecutionContext* c, MongoExtensionGetNextResult* r) noexcept {
        auto* s = static_cast<Source*>(e); s->calls++;
        // real DocumentSource::getNext() calls checkForInterrupt on every pull
        St in = static_cast<Ctx*>(c)->interrupted();
        if (in.code) return hErr(in.code, in.reason);
        r->resultDocument = {kByteView, {}}; r->resultMetadata = {kByteView, {}};
        s->viewStore.assign(64, 0xEE);  // poison the previous view
        if ((int)s->i == s->failAt) return hErr(13435, "injected host source failure");
        if (s->i >= s->docs.size()) { r->code = kEOF; return hOk(); }
        const Bytes& d = s->docs[s->i];
        bool buf = s->kind == "buf" || (s->kind == "alt" && s->i % 2 == 0);
        if (buf) { r->resultDocument.type = kByteBuf; r->resultDocument.bytes.buf = new HBuf(d); }
        else { s->viewStore = d; r->resultDocument.bytes.view = view(s->viewStore); }
        if (s->i % 3 == 0) {  // metadata on every third doc
            mb::Writer w; w.i32("$srcIndex", (int32_t)s->i); Bytes m = w.finish();
            if (buf) { r->resultMetadata.type = kByteBuf; r->resultMetadata.bytes.buf = new HBuf(m); }
            else { s->metaStore = m; r->resultMetadata.bytes.view = view(s->metaStore); }
        }
        r->code = kAdvanced; s->i++;
        return hOk();
    }
    static void destroy(MongoExtensionExecAggStage*) noexcept {}
    static MongoExtensionByteView name(const MongoExtensionExecAggStage*) noexcept { return view("hostSource", 10); }
    static MongoExtensionStatus* metrics(const MongoExtensionExecAggStage*, MongoExtensionOperationMetrics**) noexcept { return hErr(1, "unsupported"); }
    static MongoExtensionStatus* setSource(MongoExtensionExecAggStage*, MongoExtensionExecAggStage*) noexcept { return hErr(1, "unsupported"); }
    static MongoExtensionStatus* nop(MongoExtensionExecAggStage*) noexcept { return hOk(); }
    static MongoExtensionStatus* explain(const MongoExtensionExecAggStage*, MongoExtensionQueryExecutionContext*, MongoExtensionExplainVerbosity, MongoExtensionByteBuf**) noexcept { return hErr(1, "unsupported"); }
    static const MongoExtensionExecAggStageVTable kVT;
};
const MongoExtensionExecAggStageVTable Source::kVT{&Source::destroy, &Source::getNext, &Source::name, &Source::metrics, &Source::setSource, &Source::nop, &Source::nop, &Source::nop, &Source::explain};

// ------------------------------------------------------------ host services + portal
static std::vector<std::string> gHostLog;
struct Logger : MongoExtensionLogger {
    Logger() : MongoExtensionLogger{&kVT} {}
    static MongoExtensionStatus* log(const MongoExtensionLogMessage* m) noexcept { gHostLog.push_back(toStr(m->message)); return hOk(); }
    static MongoExtensionStatus* should(MongoExtensionLogSeverity, MongoExtensionLogType, bool* o) noexcept { *o = true; return hOk(); }
    static const MongoExtensionLoggerVTable kVT;
};
const MongoExtensionLoggerVTable Logger::kVT{&Logger::log, &Logger::should};
static Logger gLogger;
static MongoExtensionStatus* hsUnsupported(const char* what) { return hErr(1, std::string("fake host: ") + what + " unsupported"); }
static const MongoExtensionHostServicesVTable kServicesVT{
    +[]() -> MongoExtensionLogger* { return &gLogger; },
    +[](MongoExtensionByteView) -> MongoExtensionStatus* { return hErr(2, "user_asserted"); },
    +[](MongoExtensionByteView) -> MongoExtensionStatus* { return hErr(3, "tripwire_asserted"); },
    +[](MongoExtensionIdleThreadBlock**, const char*) -> MongoExtensionStatus* { return hsUnsupported("mark_idle_thread_block"); },
    +[](MongoExtensionByteView, MongoExtensionAggStageParseNode**) -> MongoExtensionStatus* { return hsUnsupported("create_host_agg_stage_parse_node"); },
    +[](MongoExtensionByteView, MongoExtensionAggStageAstNode**) -> MongoExtensionStatus* { return hsUnsupported("create_id_lookup"); },
    +[](MongoExtensionByteView, MongoExtensionDocResultsDPLCallback, void*, void (*)(void*), MongoExtensionAggStageAstNode**) -> MongoExtensionStatus* { return hsUnsupported("create_document_results_and_metadata"); },
};
static MongoExtensionHostServices gServices{&kServicesVT};

struct Portal : MongoExtensionHostPortal {
    std::string options = "defaultBatch: 4\n";  // the real host passes the extensionOptions YAML as text
    std::vector<const MongoExtensionAggStageDescriptor*> registered;
    long ruleCalls = 0;
    Portal() : MongoExtensionHostPortal{&kVT, {1, 0}, 0} {}
    static MongoExtensionStatus* reg(const MongoExtensionHostPortal* p, const MongoExtensionAggStageDescriptor* d) noexcept {
        auto* self = const_cast<Portal*>(static_cast<const Portal*>(p));
        // the real host asserts these three slots are non-null (shared/handle/aggregation_stage/stage_descriptor.h:62)
        if (!d || !d->vtable || !d->vtable->get_name || !d->vtable->parse || !d->vtable->get_client_type) return hErr(10, "invalid descriptor vtable");
        self->registered.push_back(d); return hOk();
    }
    static MongoExtensionByteView opts(const MongoExtensionHostPortal* p) noexcept { return view(static_cast<const Portal*>(p)->options); }
    static MongoExtensionStatus* rules(const MongoExtensionHostPortal* p, MongoExtensionByteView, const MongoExtensionPipelineRewriteRule*, size_t) noexcept { const_cast<Portal*>(static_cast<const Portal*>(p))->ruleCalls++; return hOk(); }
    static const MongoExtensionHostPortalVTable kVT;
};
const MongoExtensionHostPortalVTable Portal::kVT{&Portal::reg, &Portal::opts, &Portal::rules};

// ------------------------------------------------------------ vtable audit (mirrors every assertVTableConstraints in shared/handle)
static int gSlots = 0, gMissing = 0;
static void audit(const char* ty, const char* slot, const void* fn) {
    ++gSlots; if (!fn) { ++gMissing; printf("  [FAIL] %s.%s is NULL\n", ty, slot); ++gFail; }
}
#define A(T, vt, slot) audit(T, #slot, (const void*)(vt)->slot)

// ------------------------------------------------------------ mesh generators
static Bytes cubeMesh(double s) {
    double v[8][3] = {{0,0,0},{s,0,0},{s,s,0},{0,s,0},{0,0,s},{s,0,s},{s,s,s},{0,s,s}};
    uint32_t t[12][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}};
    uint32_t nv = 8, nt = 12; Bytes b(8 + 24 * nv + 12 * nt);
    memcpy(&b[0], &nv, 4); memcpy(&b[4], &nt, 4); memcpy(&b[8], v, sizeof v); memcpy(&b[8 + 24 * nv], t, sizeof t);
    return b;
}
static Bytes makeDoc(int i, const Bytes& mesh) {
    mb::Writer w; w.i32("_id", i); w.string("name", "mesh_" + std::to_string(i)); w.binary("mesh", mesh.data(), mesh.size()); return w.finish();
}
static double sideOf(int i) { return 1.0 + (i % 5) * 0.5; }
static std::vector<Bytes> makeDocs(int n) { std::vector<Bytes> d; for (int i = 0; i < n; ++i) d.push_back(makeDoc(i, cubeMesh(sideOf(i)))); return d; }
static Bytes stageSpec(const std::string& inner) {  // {$meshMeasure: {<inner>}} built from simple "k=v" list
    mb::Writer w; size_t o = w.begin("$meshMeasure");
    size_t p = 0;
    while (p < inner.size()) {
        size_t e = inner.find(',', p); if (e == std::string::npos) e = inner.size();
        std::string kv = inner.substr(p, e - p), k = kv.substr(0, kv.find('=')), v = kv.substr(kv.find('=') + 1);
        if (v == "true" || v == "false") w.boolean(k.c_str(), v == "true");
        else if (!v.empty() && (isdigit(v[0]))) w.i32(k.c_str(), atoi(v.c_str()));
        else w.string(k.c_str(), v);
        p = e + 1;
    }
    w.end(o); return w.finish();
}

// ------------------------------------------------------------ stage under test (host-side wrapper over the C objects)
struct Stage {
    MongoExtensionAggStageParseNode* parse = nullptr;
    MongoExtensionAggStageAstNode* ast = nullptr;
    MongoExtensionLogicalAggStage* logical = nullptr;
    MongoExtensionExecAggStage* exec = nullptr;
    St err{0, ""}; std::string where;
    Bytes props, shape;
    ~Stage() {
        if (exec) CALL("ExecAggStage", exec, destroy);
        if (logical) CALL("LogicalAggStage", logical, destroy);
        if (ast) CALL("AstNode", ast, destroy);
        if (parse) CALL("ParseNode", parse, destroy);
    }
};
static const MongoExtensionAggStageDescriptor* gDesc = nullptr;
static ShapeOpts gShape;

static std::unique_ptr<Stage> build(const Bytes& spec) {
    auto s = std::make_unique<Stage>();
    St e = consume(CALL("Descriptor", gDesc, parse, view(spec), &s->parse));
    if (e.code) { s->err = e; s->where = "parse"; s->parse = nullptr; return s; }
    // query shape
    MongoExtensionByteBuf* sb = nullptr;
    e = consume(CALL("ParseNode", s->parse, get_query_shape, &gShape, &sb)); if (e.code) { s->err = e; s->where = "shape"; return s; }
    s->shape = consumeBuf(sb);
    // expand -> container -> transfer
    MongoExtensionExpandedArrayContainer* cont = nullptr;
    e = consume(CALL("ParseNode", s->parse, expand, &cont)); if (e.code) { s->err = e; s->where = "expand"; return s; }
    size_t n = CALL("ExpandedArrayContainer", cont, size);
    std::vector<MongoExtensionExpandedArrayElement> el(n);
    MongoExtensionExpandedArray arr{n, el.data()};
    e = consume(CALL("ExpandedArrayContainer", cont, transfer, &arr));
    CALL("ExpandedArrayContainer", cont, destroy);
    if (e.code) { s->err = e; s->where = "transfer"; return s; }
    if (n != 1 || el[0].type != kAstNode) { s->err = {-1, "expected one AstNode"}; s->where = "expand"; return s; }
    s->ast = el[0].parseOrAst.ast;
    MongoExtensionByteBuf* pb = nullptr;
    e = consume(CALL("AstNode", s->ast, get_properties, &pb)); if (e.code) { s->err = e; s->where = "props"; return s; }
    s->props = consumeBuf(pb);
    MongoExtensionCatalogContext cc{{view("test", 4), view("meshes", 6)}, view("uuid", 4), 0, kNotExplain, view(nullptr, 0), 0};
    e = consume(CALL("AstNode", s->ast, promote, &cc, &s->logical)); if (e.code) { s->err = e; s->where = "promote"; return s; }
    e = consume(CALL("LogicalAggStage", s->logical, compile, &s->exec)); if (e.code) { s->err = e; s->where = "compile"; return s; }
    return s;
}

struct Run {
    std::vector<Bytes> docs, metas;
    int code = 0; std::string reason;
    long getNextCalls = 0, pauses = 0; bool eof = false;
    std::vector<long> srcCallsAtOutput;
    double ms = 0;
};
static Run drive(Stage& st, Ctx& ctx, Source& src, int maxCalls = 100000) {
    Run r; auto t0 = Clock::now();
    CALL("ExecAggStage", st.exec, set_source, &src);  // result ignored: host source status; extension returns OK singleton
    for (int c = 0; c < maxCalls; ++c) {
        MongoExtensionGetNextResult res{}; res.code = kEOF; res.resultDocument = {kByteView, {}}; res.resultMetadata = {kByteView, {}};
        MongoExtensionStatus* s = CALL("ExecAggStage", st.exec, get_next, &ctx, &res);
        r.getNextCalls++;
        St e{s ? s->vtable->get_code(s) : -999, s ? toStr(s->vtable->get_reason(s)) : "NULL"};
        if (s) s->vtable->destroy(s);
        auto copy = [](MongoExtensionByteContainer& c2, Bytes& dst) {  // what ExtensionStage::doGetNext does (getOwned), then release
            if (c2.type == kByteBuf) { if (c2.bytes.buf) dst = consumeBuf(c2.bytes.buf); }
            else if (c2.bytes.view.data && c2.bytes.view.len) dst.assign(c2.bytes.view.data, c2.bytes.view.data + c2.bytes.view.len);
        };
        if (e.code) { r.code = e.code; r.reason = e.reason; break; }
        Bytes d, m;
        copy(res.resultDocument, d); copy(res.resultMetadata, m);
        if (res.code == kAdvanced) { r.docs.push_back(d); r.metas.push_back(m); r.srcCallsAtOutput.push_back(src.calls); }
        else if (res.code == kPauseExecution) r.pauses++;
        else { r.eof = true; break; }
    }
    r.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return r;
}
static double volumeOf(const Bytes& d) { mb::Doc doc{d.data(), d.size()}; mb::Elem e; double v = NAN; if (doc.find("volume", e)) mb::num(e, v); return v; }
static std::string strField(const Bytes& d, const char* k) { mb::Doc doc{d.data(), d.size()}; mb::Elem e; return doc.find(k, e) && e.type == 2 ? mb::str(e) : ""; }
static mb::Elem execStat(const Bytes& b, const char* k) { mb::Doc d{b.data(), b.size()}; mb::Elem e{}; d.find(k, e); return e; }
static long statI(const Bytes& b, const char* k) { double v = -1; auto e = execStat(b, k); if (e.val) mb::num(e, v); return (long)v; }
static Bytes explainExec(Stage& st, Ctx& ctx) {
    MongoExtensionByteBuf* b = nullptr;
    St e = consume(CALL("ExecAggStage", st.exec, explain, &ctx, kExecStats, &b)); (void)e;
    return consumeBuf(b);
}
static bool allVolumesRight(const Run& r, int n) {
    if ((int)r.docs.size() != n) return false;
    for (int i = 0; i < n; ++i) { double want = std::pow(sideOf(i), 3); if (std::fabs(volumeOf(r.docs[i]) - want) > 1e-9 * want || strField(r.docs[i], "name") != "mesh_" + std::to_string(i)) return false; }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: fake_host ext.dylib helper.dylib\n"); return 2; }
    const std::string extPath = argv[1], helperPath = argv[2];
    printf("== A. load, negotiate, register ==\n");
    void* lib = dlopen(extPath.c_str(), RTLD_NOW | RTLD_LOCAL);  // same flags as platform/shared_library_posix.cpp:39
    CHECK(lib != nullptr, "dlopen(RTLD_NOW|RTLD_LOCAL) %s", lib ? "ok" : dlerror());
    if (!lib) return 1;
    auto getVersions = (get_mongodb_extension_versions_t)dlsym(lib, GET_MONGODB_EXTENSION_VERSIONS_SYMBOL);
    auto getExt = (get_mongo_extension_t)dlsym(lib, GET_MONGODB_EXTENSION_SYMBOL);
    CHECK(getVersions && getExt, "dlsym get_mongodb_extension_versions / get_mongodb_extension");
    // dlsym of a symbol that must NOT be exported (hidden visibility): proves only entry points leak out
    CHECK(dlsym(lib, "_ZN12_GLOBAL__N_14Exec7getNextE") == nullptr, "internal symbols are not visible to dlsym");

    MongoExtensionAPIVersionVector vv{0, nullptr};
    getVersions(&vv);                                   // Phase 1
    printf("  extension advertises %llu version(s):", (unsigned long long)vv.len);
    for (uint64_t i = 0; i < vv.len; ++i) printf(" %u.%u", vv.versions[i].major, vv.versions[i].minor);
    printf("\n");
    const MongoExtensionAPIVersion hostSupported{MONGODB_EXTENSION_API_MAJOR_VERSION, MONGODB_EXTENSION_API_MINOR_VERSION};
    MongoExtensionAPIVersion chosen{0, 0}; bool found = false;
    for (uint64_t i = 0; i < vv.len; ++i) if (vv.versions[i].major == hostSupported.major && vv.versions[i].minor <= hostSupported.minor && !(found && vv.versions[i].minor < chosen.minor)) { chosen = vv.versions[i]; found = true; }
    CHECK(found, "negotiated version %u.%u (major must match, extension minor <= host minor)", chosen.major, chosen.minor);

    const MongoExtension* ext = nullptr;
    // an unadvertised version is documented UB (api.h Phase 2 comment); this extension rejects it with a status
    { const MongoExtension* tmp = nullptr; St e = consume(getExt({2, 0}, &gServices, &tmp)); CHECK(e.code != 0 && tmp == nullptr, "Phase 2 with unadvertised 2.0 -> code %d '%s'", e.code, e.reason.c_str()); }
    St e2 = consume(getExt(chosen, &gServices, &ext));  // Phase 2
    CHECK(e2.code == 0 && ext != nullptr, "get_mongodb_extension(%u.%u) code=%d", chosen.major, chosen.minor, e2.code);
    CHECK(ext && ext->version.major == chosen.major && ext->version.minor == chosen.minor, "extension->version == negotiated (host uassert 13693501)");
    A("MongoExtension", ext->vtable, initialize);
    Portal portal;
    St e3 = consume(CALL("MongoExtension", ext, initialize, &portal));
    CHECK(e3.code == 0, "initialize() code=%d %s", e3.code, e3.reason.c_str());
    CHECK(portal.registered.size() == 1, "exactly one stage descriptor registered (got %zu)", portal.registered.size());
    gDesc = portal.registered.at(0);
    A("AggStageDescriptor", gDesc->vtable, get_name); A("AggStageDescriptor", gDesc->vtable, get_client_type); A("AggStageDescriptor", gDesc->vtable, parse);
    auto dn = CALL("Descriptor", gDesc, get_name);
    CHECK(toStr(dn) == "$meshMeasure", "descriptor name '%s'", toStr(dn).c_str());
    CHECK(CALL("Descriptor", gDesc, get_client_type) == kMongoExtensionClientTypeAny, "client type = Any (user pipelines may use it)");

    printf("\n== B. parse -> expand -> promote -> compile, static properties, vtable audit ==\n");
    Ctx ctx; Source src; src.docs = makeDocs(20);
    {
        auto st = build(stageSpec("batch=8"));
        CHECK(st->err.code == 0, "build ok (%s %d %s)", st->where.c_str(), st->err.code, st->err.reason.c_str());
        mb::Doc q{st->shape.data(), st->shape.size()}; mb::Elem e{}; q.find("$meshMeasure", e);
        mb::Elem f{}; mb::subdoc(e).find("field", f);
        CHECK(q.valid() && f.type == 2 && mb::str(f) == "HASH<mesh>", "query shape routed 'field' through host serialize_field_path -> '%s'", f.type == 2 ? mb::str(f).c_str() : "?");
        // static properties vs public/extension_agg_stage_static_properties.idl
        mb::Doc pd{st->props.data(), st->props.size()}; size_t c = 0; mb::Elem pe; std::set<std::string> known{"streamType","position","hostType","requiresInputDocSource","requiredPrivileges","requiredMetadataFields","providedMetadataFields","preservesUpstreamMetadata","allowedInUnionWith","allowedInLookup","allowedInFacet","isSelectionStage"};
        bool allKnown = true; std::string dump;
        while (pd.next(c, pe)) { allKnown &= known.count(pe.name) > 0; dump += std::string(pe.name) + " "; }
        mb::Elem x{};
        pd.find("streamType", x); CHECK(x.type == 2 && (mb::str(x) == "streaming" || mb::str(x) == "blocking"), "streamType '%s' valid enum", mb::str(x).c_str());
        pd.find("position", x); CHECK(mb::str(x) == "none", "position none");
        pd.find("hostType", x); CHECK(mb::str(x) == "none", "hostType none");
        CHECK(allKnown, "all property keys known to the IDL: %s", dump.c_str());
        pd.find("requiredPrivileges", x); mb::Elem p0{}; mb::subdoc(x).find("0", p0); mb::Elem acts{}; mb::subdoc(p0).find("actions", acts); mb::Elem a0{}; mb::subdoc(acts).find("0", a0); mb::Elem act{}; mb::subdoc(a0).find("action", act);
        CHECK(act.type == 2 && mb::str(act) == "find", "requiredPrivileges[0].actions[0].action = '%s' (extension_privilege.idl enum)", act.type == 2 ? mb::str(act).c_str() : "?");
        // audit every vtable the host asserts on
        auto* P = st->parse->vtable; A("ParseNode", P, get_name); A("ParseNode", P, get_query_shape); A("ParseNode", P, expand); A("ParseNode", P, clone); A("ParseNode", P, to_bson_for_log); A("ParseNode", P, destroy);
        auto* AS = st->ast->vtable; A("AstNode", AS, get_name); A("AstNode", AS, get_properties); A("AstNode", AS, promote); A("AstNode", AS, clone); A("AstNode", AS, get_first_stage_view_application_policy); A("AstNode", AS, bind_resolved_namespace); A("AstNode", AS, destroy);
        auto* L = st->logical->vtable; A("LogicalAggStage", L, get_name); A("LogicalAggStage", L, serialize); A("LogicalAggStage", L, explain); A("LogicalAggStage", L, compile); A("LogicalAggStage", L, get_distributed_plan_logic); A("LogicalAggStage", L, clone); A("LogicalAggStage", L, set_vector_search_limit_for_optimization_deprecated); A("LogicalAggStage", L, evaluate_pipeline_rewrite_rule_precondition); A("LogicalAggStage", L, evaluate_pipeline_rewrite_rule_transform); A("LogicalAggStage", L, get_filter); A("LogicalAggStage", L, apply_pipeline_suffix_dependencies); A("LogicalAggStage", L, get_sort_pattern); A("LogicalAggStage", L, skip_stream); A("LogicalAggStage", L, get_docs_needed_bounds); A("LogicalAggStage", L, destroy);
        auto* X = st->exec->vtable; A("ExecAggStage", X, get_next); A("ExecAggStage", X, get_name); A("ExecAggStage", X, create_metrics); A("ExecAggStage", X, set_source); A("ExecAggStage", X, open); A("ExecAggStage", X, reopen); A("ExecAggStage", X, close); A("ExecAggStage", X, explain); A("ExecAggStage", X, destroy);
        // exercise the remaining logical/ast/parse slots
        MongoExtensionByteBuf* b = nullptr;
        consume(CALL("LogicalAggStage", st->logical, serialize, &b)); Bytes ser = consumeBuf(b);
        CHECK(ser == stageSpec("batch=8"), "LogicalAggStage.serialize round-trips the stage definition");
        { auto again = build(ser); CHECK(again->err.code == 0, "serialized form re-parses (what a shard receives from mongos)"); }
        consume(CALL("LogicalAggStage", st->logical, explain, &ctx, kQueryPlanner, &b)); CHECK(!consumeBuf(b).empty(), "LogicalAggStage.explain returns BSON");
        MongoExtensionDistributedPlanLogic* dpl = reinterpret_cast<MongoExtensionDistributedPlanLogic*>(0x1);
        consume(CALL("LogicalAggStage", st->logical, get_distributed_plan_logic, &dpl)); CHECK(dpl == nullptr, "get_distributed_plan_logic -> nullptr (runs fully on shards)");
        MongoExtensionLogicalAggStage* lc = nullptr; consume(CALL("LogicalAggStage", st->logical, clone, &lc)); CHECK(lc != nullptr, "logical clone"); if (lc) lc->vtable->destroy(lc);
        MongoExtensionAggStageParseNode* pc = nullptr; consume(CALL("ParseNode", st->parse, clone, &pc)); CHECK(pc != nullptr, "parse clone"); if (pc) pc->vtable->destroy(pc);
        MongoExtensionAggStageAstNode* ac = nullptr; consume(CALL("AstNode", st->ast, clone, &ac)); CHECK(ac != nullptr, "ast clone"); if (ac) ac->vtable->destroy(ac);
        consume(CALL("ParseNode", st->parse, to_bson_for_log, &b)); CHECK(!consumeBuf(b).empty(), "to_bson_for_log");
        bool r = true; MongoExtensionByteView rn = view("anything", 8); consume(CALL("LogicalAggStage", st->logical, evaluate_pipeline_rewrite_rule_precondition, rn, nullptr, &r)); CHECK(!r, "no rewrite rules fire");
        consume(CALL("LogicalAggStage", st->logical, get_docs_needed_bounds, &b)); Bytes db = consumeBuf(b); mb::Elem eff{}; mb::Doc{db.data(), db.size()}.find("effect", eff); CHECK(eff.type == 2 && mb::str(eff) == "noEffect", "docsNeededBounds effect noEffect");
        MongoExtensionOperationMetrics* m = nullptr; consume(CALL("ExecAggStage", st->exec, create_metrics, &m));
        if (m) { A("OperationMetrics", m->vtable, destroy); A("OperationMetrics", m->vtable, serialize); A("OperationMetrics", m->vtable, update); consume(CALL("OperationMetrics", m, update, view("x", 1))); consume(CALL("OperationMetrics", m, serialize, &b)); CHECK(statI(consumeBuf(b), "docs") == 1, "metrics object works"); CALL("OperationMetrics", m, destroy); }
        // Status vtable audit on an extension-allocated error status
        auto bad = build(stageSpec("mode=nonsense"));
        CHECK(bad->err.code == 2, "invalid spec rejected at parse: code=%d '%s'", bad->err.code, bad->err.reason.c_str());
    }
    {   // status object returned by the extension: audit its vtable slots
        MongoExtensionAggStageParseNode* pn = nullptr;
        MongoExtensionStatus* s = CALL("Descriptor", gDesc, parse, view(stageSpec("bogus=1")), &pn);
        A("Status", s->vtable, destroy); A("Status", s->vtable, get_code); A("Status", s->vtable, get_reason); A("Status", s->vtable, set_code); A("Status", s->vtable, set_reason); A("Status", s->vtable, clone);
        MongoExtensionStatus* cl = nullptr; auto* r = s->vtable->clone(s, &cl); r->vtable->destroy(r);
        CHECK(cl && cl->vtable->get_code(cl) == s->vtable->get_code(s), "status clone keeps the code");
        cl->vtable->destroy(cl); s->vtable->destroy(s);
    }
    printf("  vtable slots audited: %d, NULL: %d\n", gSlots, gMissing);

    printf("\n== C. batching: one document per get_next, N pulled from the source per dispatch ==\n");
    Bytes baseline;
    {
        Source s1; s1.docs = makeDocs(20); s1.kind = "alt"; Ctx c1;
        auto st = build(stageSpec("batch=8,out=alt"));
        Run r = drive(*st, c1, s1);
        CHECK(allVolumesRight(r, 20), "20 docs out, order kept, volume == side^3 within 1e-9 rel, other fields preserved");
        CHECK(r.eof && r.getNextCalls == 21, "get_next calls = 21 (20 docs + EOF): exactly ONE document per call");
        CHECK(!r.srcCallsAtOutput.empty() && r.srcCallsAtOutput[0] == 8, "first output only after 8 source pulls (batched gather): srcCalls@first=%ld", r.srcCallsAtOutput.empty() ? -1 : r.srcCallsAtOutput[0]);
        bool metaOk = true; for (int i = 0; i < 20; ++i) { bool want = i % 3 == 0; metaOk &= (r.metas[i].empty() != want); if (want) metaOk &= statI(r.metas[i], "$srcIndex") == i; }
        CHECK(metaOk, "upstream metadata container passed through for docs 0,3,6,...");
        Bytes ex = explainExec(*st, c1);
        CHECK(statI(ex, "dispatches") == 3 && statI(ex, "maxBatch") == 8 && statI(ex, "sourcePulls") == 21, "explain: dispatches=%ld maxBatch=%ld sourcePulls=%ld (expect 3/8/21)", statI(ex, "dispatches"), statI(ex, "maxBatch"), statI(ex, "sourcePulls"));
        for (auto& d : r.docs) { baseline.insert(baseline.end(), d.begin(), d.end()); }
    }
    for (const char* sk : {"buf", "view", "alt"}) for (const char* ok2 : {"buf", "view", "alt"}) {
        Source s2; s2.docs = makeDocs(20); s2.kind = sk; Ctx c2;
        auto st = build(stageSpec(std::string("batch=8,out=") + ok2));
        Run r = drive(*st, c2, s2); Bytes all; for (auto& d : r.docs) all.insert(all.end(), d.begin(), d.end());
        CHECK(all == baseline, "host source=%s x extension out=%s -> byte-identical output (kByteView/kByteBuf lifetimes honoured)", sk, ok2);
    }
    for (int b : {1, 5, 20, 1000}) {
        Source s3; s3.docs = makeDocs(20); Ctx c3; auto st = build(stageSpec("batch=" + std::to_string(b)));
        Run r = drive(*st, c3, s3); Bytes ex = explainExec(*st, c3);
        printf("  batch=%-4d -> dispatches=%ld maxBatch=%ld, outputs=%zu\n", b, statI(ex, "dispatches"), statI(ex, "maxBatch"), r.docs.size());
        CHECK(r.docs.size() == 20, "batch=%d yields all 20 docs", b);
    }
    {   // extension option from the host portal (YAML text) -> default batch when the stage omits it
        Source s4; s4.docs = makeDocs(20); Ctx c4; auto st = build(stageSpec("out=buf"));
        Run r = drive(*st, c4, s4); Bytes ex = explainExec(*st, c4);
        CHECK(statI(ex, "maxBatch") == 4 && statI(ex, "dispatches") == 5, "portal option 'defaultBatch: 4' honoured: maxBatch=%ld dispatches=%ld", statI(ex, "maxBatch"), statI(ex, "dispatches"));
    }
    {   // set_batch_size on the real host is rejected outside a document-results-and-metadata stream
        Ctx c5; St e = consume(CALL("QueryExecutionContext", (MongoExtensionQueryExecutionContext*)&c5, set_batch_size, 8));
        CHECK(e.code == 13150705, "ctx->set_batch_size -> %d '%s' (mirrors query_execution_context_adapter.cpp:66-70)", e.code, e.reason.c_str());
    }

    printf("\n== D. errors across the boundary ==\n");
    for (const char* bad : {"bogus=1", "mode=sideways", "batch=0", "out=file"}) {
        auto st = build(stageSpec(bad));
        CHECK(st->err.code == 2 && !st->err.reason.empty() && st->where == "parse", "parse error '%s' -> code %d reason '%s'", bad, st->err.code, st->err.reason.c_str());
    }
    { mb::Writer w; w.i32("$other", 1); auto st = build(w.finish()); CHECK(st->err.code == 2, "wrong stage name -> code %d '%s'", st->err.code, st->err.reason.c_str()); }
    { mb::Writer w; size_t o = w.begin("$meshMeasure"); w.end(o); Bytes ok3 = w.finish(); auto st = build(ok3); CHECK(st->err.code == 0, "empty argument object accepted (defaults)"); }
    {
        Source s; s.docs = makeDocs(4); Ctx c; auto st = build(stageSpec("mode=throw"));
        Run r = drive(*st, c, s);
        CHECK(r.code == -1 && r.reason.find("planted fault") != std::string::npos, "C++ exception caught at boundary -> status code %d (MONGO_EXTENSION_STATUS_RUNTIME_ERROR) '%s'", r.code, r.reason.c_str());
        Run r2 = drive(*st, c, s, 1);
        CHECK(r2.code == -1, "stage object still callable after a caught exception (second call also reports the status)");
    }
    {
        Source s; s.docs = makeDocs(4); Ctx c; auto st = build(stageSpec("mode=code")); Run r = drive(*st, c, s);
        CHECK(r.code == 11601, "extension-chosen error code preserved: %d '%s'", r.code, r.reason.c_str());
    }
    {   // bad mesh payloads inside documents
        std::vector<Bytes> docs; Bytes ok4 = cubeMesh(1); Bytes trunc(ok4.begin(), ok4.end() - 5);
        docs.push_back(makeDoc(0, ok4)); docs.push_back(makeDoc(1, trunc));
        Source s; s.docs = docs; Ctx c; auto st = build(stageSpec("batch=2")); Run r = drive(*st, c, s);
        CHECK(r.code == 2 && r.reason.find("size") != std::string::npos, "truncated mesh -> code %d '%s' (whole batch fails: no partial output, %zu docs out)", r.code, r.reason.c_str(), r.docs.size());
        mb::Writer w; w.i32("_id", 1); Source s2; s2.docs = {w.finish()}; Ctx c2; auto st2 = build(stageSpec("batch=2")); Run r2 = drive(*st2, c2, s2);
        CHECK(r2.code == 2 && r2.reason.find("no field") != std::string::npos, "missing field -> code %d '%s'", r2.code, r2.reason.c_str());
        Bytes oob = cubeMesh(1); uint32_t big = 99; memcpy(&oob[8 + 24 * 8], &big, 4);
        Source s3; s3.docs = {makeDoc(0, oob)}; Ctx c3; auto st3 = build(stageSpec("batch=2")); Run r3 = drive(*st3, c3, s3);
        CHECK(r3.code == 2 && r3.reason.find("out of range") != std::string::npos, "triangle index out of range -> code %d '%s'", r3.code, r3.reason.c_str());
    }
    {   // error status produced by the HOST (source) while the extension is gathering a batch: passed through unchanged
        Source s; s.docs = makeDocs(20); s.failAt = 5; Ctx c; auto st = build(stageSpec("batch=8")); Run r = drive(*st, c, s);
        CHECK(r.code == 13435 && r.reason == "injected host source failure", "host status passed through: code %d '%s' (destroyed via the host's own vtable)", r.code, r.reason.c_str());
    }
    {   // the exception-escape case: child process, exception thrown from a noexcept C entry point
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            Source s; s.docs = makeDocs(2); Ctx c; auto st = build(stageSpec("mode=escape")); drive(*st, c, s);
            _exit(0);
        }
        int wst = 0; waitpid(pid, &wst, 0);
        CHECK(WIFSIGNALED(wst), "exception escaping the extension's C entry point kills the process (signal %d %s): the host cannot recover", WIFSIGNALED(wst) ? WTERMSIG(wst) : 0, WIFSIGNALED(wst) ? strsignal(WTERMSIG(wst)) : "exited normally");
    }

    printf("\n== E. deadlines, killOp, no preemption ==\n");
    {   // normal mode: polls check_for_interrupt once per batch; deadline already passed -> error before any work
        Source s; s.docs = makeDocs(4); Ctx c; c.deadlineMs = nowMs() - 1000; auto st = build(stageSpec("batch=2")); Run r = drive(*st, c, s);
        CHECK(r.code == 50, "deadline in the past -> per-batch check_for_interrupt surfaces code %d '%s'", r.code, r.reason.c_str());
        Source s2; s2.docs = makeDocs(4); Ctx c2; c2.deadlineMs = INT64_MAX; auto st2 = build(stageSpec("batch=2")); Run r2 = drive(*st2, c2, s2);
        CHECK(r2.code == 0 && r2.docs.size() == 4, "no deadline (INT64_MAX) -> runs normally");
    }
    {   // 2 s per batch, does NOT poll: runs to completion although the 300 ms deadline passed long ago
        Source s; s.docs = makeDocs(3); Ctx c; c.deadlineMs = nowMs() + 300; auto st = build(stageSpec("batch=8,mode=slow,sleepMs=2000"));
        MongoExtensionGetNextResult res{}; res.resultDocument = {kByteView, {}}; res.resultMetadata = {kByteView, {}};
        CALL("ExecAggStage", st->exec, set_source, &s); auto t0 = Clock::now();
        St e = consume(CALL("ExecAggStage", st->exec, get_next, &c, &res)); double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (res.resultDocument.type == kByteBuf && res.resultDocument.bytes.buf) consumeBuf(res.resultDocument.bytes.buf);
        if (res.resultMetadata.type == kByteBuf && res.resultMetadata.bytes.buf) consumeBuf(res.resultMetadata.bytes.buf);
        St after = c.interrupted();
        CHECK(e.code == 0 && ms >= 1900, "non-polling 2 s batch: returned code %d after %.0f ms, ignoring the 300 ms deadline (host cannot preempt)", e.code, ms);
        CHECK(after.code == 50, "...host only notices afterwards: its next check_for_interrupt says %d '%s'", after.code, after.reason.c_str());
    }
    {   // same 2 s batch but polling in 20 ms slices: aborts shortly after the deadline
        Source s; s.docs = makeDocs(3); Ctx c; c.deadlineMs = nowMs() + 300; auto st = build(stageSpec("batch=8,mode=slow_poll,sleepMs=2000")); Run r = drive(*st, c, s);
        CHECK(r.code == 50 && r.ms < 700, "polling 2 s batch: code %d after %.0f ms (deadline was +300 ms) '%s'", r.code, r.ms, r.reason.c_str());
    }
    {   // killOp from another thread
        Source s; s.docs = makeDocs(3); Ctx c; auto st = build(stageSpec("batch=8,mode=slow_poll,sleepMs=2000"));
        std::thread killer([&] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); c.killed = true; });
        Run r = drive(*st, c, s); killer.join();
        CHECK(r.code == 11601 && r.ms < 700, "killOp at +200 ms -> code %d after %.0f ms '%s'", r.code, r.ms, r.reason.c_str());
    }
    {   // the context pointer is per-call: host builds a NEW QueryExecutionContextAdapter on its stack in every doGetNext (extension_stage.cpp:94-102)
        printf("  note: execCtx is valid only for the duration of one get_next (not retained by this extension)\n");
    }

    printf("\n== F. dlopen from inside the extension, own threads ==\n");
    {
        Source s; s.docs = makeDocs(20); Ctx c; auto st = build(stageSpec("batch=8,lib=" + helperPath + ",probe=true,ticker=true,worker=true,out=alt"));
        Run r = drive(*st, c, s);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        Bytes ex = explainExec(*st, c);
        Bytes all; for (auto& d : r.docs) all.insert(all.end(), d.begin(), d.end());
        mb::Elem pe = execStat(ex, "probe");
        CHECK(statI(ex, "helperLoaded") == 1 || execStat(ex, "helperLoaded").val[0] == 1, "extension dlopen()ed %s and called mesh_helper_volume via dlsym", helperPath.c_str());
        CHECK(pe.type == 2 && mb::str(pe) == "dlopen+dlsym ok", "extension dlopen(libSystem/libc)+dlsym(getpid) -> '%s'", pe.type == 2 ? mb::str(pe).c_str() : "?");
        CHECK(execStat(ex, "workerOnOtherThread").val[0] == 1, "batch computed on an extension-owned worker thread (different thread id)");
        CHECK(statI(ex, "tickerTicks") > 3, "persistent extension thread ticking: %ld ticks", statI(ex, "tickerTicks"));
        CHECK(all == baseline, "results byte-identical to the in-extension compute path");
        Source s2; s2.docs = makeDocs(2); Ctx c2; auto st2 = build(stageSpec("lib=/nonexistent/libnope.dylib")); Run r2 = drive(*st2, c2, s2);
        CHECK(r2.code == 3 && r2.reason.find("dlopen failed") != std::string::npos, "failed dlopen -> status code %d '%s'", r2.code, r2.reason.c_str());
    }

    printf("\n== G. lifecycle: open/reopen/close (the real host never calls them; see REPORT) ==\n");
    {
        Source s; s.docs = makeDocs(6); Ctx c; auto st = build(stageSpec("batch=4"));
        consume(CALL("ExecAggStage", st->exec, open)); Run r = drive(*st, c, s);
        s.i = 0; consume(CALL("ExecAggStage", st->exec, reopen)); Run r2 = drive(*st, c, s);
        consume(CALL("ExecAggStage", st->exec, close)); Bytes ex = explainExec(*st, c);
        CHECK(r.docs.size() == 6 && r2.docs.size() == 6, "open -> run -> reopen -> run -> close works (6 + 6 docs)");
        CHECK(statI(ex, "openCalls") == 1 && statI(ex, "closeCalls") == 1, "open/close counted by the extension");
    }

    printf("\n== H. memory ownership: allocation balance ==\n");
    {
        long ea = -1, ef = -1; auto fn = (void (*)(long*, long*))dlsym(lib, "mesh_ext_test_counters");
        if (fn) fn(&ea, &ef);
        CHECK(fn != nullptr && ea == ef && ea > 0, "extension-side heap objects handed across the boundary: allocs=%ld frees=%ld (every one destroyed by the host through the vtable)", ea, ef);
        CHECK(hAllocs == hFrees, "host-side statuses/buffers given to the extension: allocs=%ld frees=%ld (extension destroyed all through the host vtable)", (long)hAllocs, (long)hFrees);
    }

    printf("\n== vtable entries the harness called (type.slot: count) ==\n");
    for (auto& kv : gCalls) printf("  %-72s %d\n", kv.first.c_str(), kv.second);
    printf("\n== host log lines from extension: %zu ==\n", gHostLog.size());
    printf("\nRESULT: %d passed, %d failed\n", gPass, gFail);
    dlclose(lib);
    return gFail ? 1 : 0;
}
