// MP14a: minimal MongoDB extension implementing the Extensions API C ABI directly.
// Includes ONLY the two public C headers (plus std/POSIX). No mongo/base, no C++ SDK.
//
// Stage: {$meshMeasure: {field: "mesh", as: "volume", batch: N, mode: ..., out: ..., lib: ..., probe: bool, ticker: bool}}
// Transform stage. Pulls up to `batch` input documents from its source inside ONE get_next call (the API returns
// exactly one document per get_next), computes the whole batch in one "dispatch" (stand-in for a GPU dispatch),
// then drains the batch one document per call.
#include "mongo/db/extension/public/api.h"

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "mesh_format.h"
#include "mini_bson.h"

#define EXPORT extern "C" __attribute__((visibility("default")))

namespace {

std::atomic<long> gAllocs{0}, gFrees{0};  // every heap object handed across the boundary
const MongoExtensionHostServices* gHostServices = nullptr;  // valid for the extension lifetime (api.h:1272-)
int gDefaultBatch = 8;

struct MeshError : std::runtime_error {  // like a uassert: carries a code
    int32_t code;
    MeshError(int32_t c, const std::string& m) : std::runtime_error(m), code(c) {}
};

MongoExtensionByteView view(const void* p, size_t n) { return MongoExtensionByteView{(const uint8_t*)p, n}; }
MongoExtensionByteView view(const std::string& s) { return view(s.data(), s.size()); }
MongoExtensionByteView view(const std::vector<uint8_t>& v) { return view(v.data(), v.size()); }
std::string toString(MongoExtensionByteView v) { return std::string((const char*)v.data, v.len); }

// ---------------------------------------------------------------- Status
struct Status : MongoExtensionStatus {
    int32_t code;
    std::string reason;
    Status(int32_t c, std::string r) : MongoExtensionStatus{&kVT}, code(c), reason(std::move(r)) {}
    static void destroy(MongoExtensionStatus* s) noexcept { delete static_cast<Status*>(s); gFrees++; }
    static int32_t getCode(const MongoExtensionStatus* s) noexcept { return static_cast<const Status*>(s)->code; }
    static MongoExtensionByteView getReason(const MongoExtensionStatus* s) noexcept {
        return view(static_cast<const Status*>(s)->reason);
    }
    static void setCode(MongoExtensionStatus* s, int32_t c) noexcept { static_cast<Status*>(s)->code = c; }
    static MongoExtensionStatus* setReason(MongoExtensionStatus* s, MongoExtensionByteView r) noexcept;
    static MongoExtensionStatus* clone(const MongoExtensionStatus* s, MongoExtensionStatus** out) noexcept;
    static const MongoExtensionStatusVTable kVT;
};
// OK is a static singleton whose destroy() is a no-op (what the SDK does too: shared/extension_status.h:24-90).
struct OkStatus : MongoExtensionStatus {
    OkStatus() : MongoExtensionStatus{&kVT} {}
    static void destroy(MongoExtensionStatus*) noexcept {}
    static int32_t getCode(const MongoExtensionStatus*) noexcept { return MONGO_EXTENSION_STATUS_OK; }
    static MongoExtensionByteView getReason(const MongoExtensionStatus*) noexcept { return view(nullptr, 0); }
    static void setCode(MongoExtensionStatus*, int32_t) noexcept {}
    static MongoExtensionStatus* setReason(MongoExtensionStatus* s, MongoExtensionByteView) noexcept;
    static MongoExtensionStatus* clone(const MongoExtensionStatus* s, MongoExtensionStatus** out) noexcept;
    static const MongoExtensionStatusVTable kVT;
};
OkStatus gOk;
MongoExtensionStatus* ok() noexcept { return &gOk; }
MongoExtensionStatus* err(int32_t code, std::string reason) noexcept {
    try { gAllocs++; return new Status(code, std::move(reason)); } catch (...) { gAllocs--; return nullptr; }
}
MongoExtensionStatus* Status::setReason(MongoExtensionStatus* s, MongoExtensionByteView r) noexcept {
    static_cast<Status*>(s)->reason = toString(r); return ok();
}
MongoExtensionStatus* Status::clone(const MongoExtensionStatus* s, MongoExtensionStatus** out) noexcept {
    auto* st = static_cast<const Status*>(s); *out = err(st->code, st->reason); return ok();
}
MongoExtensionStatus* OkStatus::setReason(MongoExtensionStatus*, MongoExtensionByteView) noexcept {
    return err(-1, "setReason on OK status is forbidden");
}
MongoExtensionStatus* OkStatus::clone(const MongoExtensionStatus*, MongoExtensionStatus** out) noexcept {
    *out = &gOk; return ok();
}
const MongoExtensionStatusVTable Status::kVT{&Status::destroy, &Status::getCode, &Status::getReason,
                                              &Status::setCode, &Status::setReason, &Status::clone};
const MongoExtensionStatusVTable OkStatus::kVT{&OkStatus::destroy, &OkStatus::getCode, &OkStatus::getReason,
                                                &OkStatus::setCode, &OkStatus::setReason, &OkStatus::clone};

// The ONE place exceptions are stopped. Every vtable function below is noexcept and goes through this.
template <typename F>
MongoExtensionStatus* guarded(F&& f) noexcept {
    try {
        f();
        return ok();
    } catch (const MeshError& e) {
        return err(e.code, e.what());
    } catch (const std::exception& e) {
        return err(MONGO_EXTENSION_STATUS_RUNTIME_ERROR, std::string("exception: ") + e.what());
    } catch (...) {
        return err(MONGO_EXTENSION_STATUS_RUNTIME_ERROR, "unknown exception");
    }
}

// ---------------------------------------------------------------- ByteBuf (extension-allocated, host destroys via vtable)
struct OwnedBuf : MongoExtensionByteBuf {
    std::vector<uint8_t> bytes;
    explicit OwnedBuf(std::vector<uint8_t> b) : MongoExtensionByteBuf{&kVT}, bytes(std::move(b)) { gAllocs++; }
    static void destroy(MongoExtensionByteBuf* b) noexcept { delete static_cast<OwnedBuf*>(b); gFrees++; }
    static MongoExtensionByteView getView(const MongoExtensionByteBuf* b) noexcept {
        return view(static_cast<const OwnedBuf*>(b)->bytes);
    }
    static const MongoExtensionByteBufVTable kVT;
};
const MongoExtensionByteBufVTable OwnedBuf::kVT{&OwnedBuf::destroy, &OwnedBuf::getView};
MongoExtensionByteBuf* makeBuf(std::vector<uint8_t> b) { return new OwnedBuf(std::move(b)); }

// ---------------------------------------------------------------- Spec
struct Spec {
    std::string stageName = "$meshMeasure";
    std::string field = "mesh", as = "volume";
    std::string mode = "normal";  // normal | throw | code | escape | slow | slow_poll
    std::string out = "buf";      // buf | view | alt   (how get_next returns the document)
    std::string lib;              // optional shared library to dlopen() lazily; must export mesh_helper_volume
    int batch = gDefaultBatch;
    int sleepMs = 2000;
    bool probe = false;           // dlopen libSystem/libc and call getpid() through dlsym
    bool ticker = false;          // persistent extension-owned background thread
    bool worker = false;          // compute each batch on a short-lived extension-owned thread
    std::vector<uint8_t> bson;    // {$meshMeasure: {...}} owned copy (the host's view is only valid during the call)
};

Spec parseSpec(MongoExtensionByteView stageBson) {
    mb::Doc d{stageBson.data, (size_t)stageBson.len};
    if (!d.valid()) throw MeshError(2, "stage definition is not a valid BSON document");
    size_t c = 0; mb::Elem e;
    if (!d.next(c, e)) throw MeshError(2, "empty stage definition");
    if (strcmp(e.name, "$meshMeasure") != 0) throw MeshError(2, std::string("unexpected stage name ") + e.name);
    if (e.type != 0x03) throw MeshError(2, "$meshMeasure requires an object argument");
    Spec s;
    mb::Doc args = mb::subdoc(e);
    size_t c2 = 0; mb::Elem a; double n;
    while (args.next(c2, a)) {
        std::string k = a.name;
        if (k == "field" && a.type == 0x02) s.field = mb::str(a);
        else if (k == "as" && a.type == 0x02) s.as = mb::str(a);
        else if (k == "mode" && a.type == 0x02) s.mode = mb::str(a);
        else if (k == "out" && a.type == 0x02) s.out = mb::str(a);
        else if (k == "lib" && a.type == 0x02) s.lib = mb::str(a);
        else if (k == "batch" && mb::num(a, n)) { if (n < 1 || n > 1e6) throw MeshError(2, "batch must be in [1, 1e6]"); s.batch = (int)n; }
        else if (k == "sleepMs" && mb::num(a, n)) s.sleepMs = (int)n;
        else if (k == "probe" && a.type == 0x08) s.probe = mb::boolean(a);
        else if (k == "ticker" && a.type == 0x08) s.ticker = mb::boolean(a);
        else if (k == "worker" && a.type == 0x08) s.worker = mb::boolean(a);
        else throw MeshError(2, "$meshMeasure: unknown or mistyped field '" + k + "'");
    }
    const char* modes[] = {"normal", "throw", "code", "escape", "slow", "slow_poll"};
    bool okMode = false; for (auto m : modes) okMode |= (s.mode == m);
    if (!okMode) throw MeshError(2, "$meshMeasure: bad mode '" + s.mode + "'");
    if (s.out != "buf" && s.out != "view" && s.out != "alt") throw MeshError(2, "$meshMeasure: bad out '" + s.out + "'");
    s.bson.assign(stageBson.data, stageBson.data + d.size());
    return s;
}

// ---------------------------------------------------------------- Operation metrics
struct Metrics : MongoExtensionOperationMetrics {
    std::atomic<long> docs{0};
    Metrics() : MongoExtensionOperationMetrics{&kVT} { gAllocs++; }
    static void destroy(MongoExtensionOperationMetrics* m) noexcept { delete static_cast<Metrics*>(m); gFrees++; }
    static MongoExtensionStatus* serialize(const MongoExtensionOperationMetrics* m, MongoExtensionByteBuf** out) noexcept {
        return guarded([&] { mb::Writer w; w.i64("docs", static_cast<const Metrics*>(m)->docs); *out = makeBuf(w.finish()); });
    }
    static MongoExtensionStatus* update(MongoExtensionOperationMetrics* m, MongoExtensionByteView) noexcept {
        static_cast<Metrics*>(m)->docs++; return ok();
    }
    static const MongoExtensionOperationMetricsVTable kVT;
};
const MongoExtensionOperationMetricsVTable Metrics::kVT{&Metrics::destroy, &Metrics::serialize, &Metrics::update};

// ---------------------------------------------------------------- ExecAggStage
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct Pending { std::vector<uint8_t> doc, meta; };

struct Exec : MongoExtensionExecAggStage {
    Spec spec;
    MongoExtensionExecAggStage* source = nullptr;  // host-owned, NOT destroyed by us (api.h set_source)
    std::deque<Pending> ready;
    bool eof = false;
    std::vector<uint8_t> lastOut;  // backing store for a returned kByteView: valid until the next get_next
    long emitted = 0, dispatches = 0, maxBatch = 0, openCalls = 0, closeCalls = 0, reopenCalls = 0, sourcePulls = 0;
    // lazily-initialised resources (the real host never calls open(), see REPORT.md)
    bool inited = false;
    void* libHandle = nullptr;
    int (*helperVolume)(const uint8_t*, uint64_t, double*) = nullptr;
    std::string probeResult = "off";
    std::thread tick; std::atomic<bool> stopTick{false}; std::atomic<long> ticks{0};
    bool workerUsed = false;
    std::thread::id callerThread, workerThread;

    explicit Exec(Spec s) : MongoExtensionExecAggStage{&kVT}, spec(std::move(s)) { gAllocs++; }
    ~Exec() {
        stopTick = true; if (tick.joinable()) tick.join();
        if (libHandle) dlclose(libHandle);
    }

    static void destroy(MongoExtensionExecAggStage* e) noexcept { delete static_cast<Exec*>(e); gFrees++; }
    static MongoExtensionByteView getName(const MongoExtensionExecAggStage*) noexcept { return view("$meshMeasure", 12); }
    static MongoExtensionStatus* createMetrics(const MongoExtensionExecAggStage*, MongoExtensionOperationMetrics** m) noexcept {
        return guarded([&] { *m = new Metrics(); });
    }
    static MongoExtensionStatus* setSource(MongoExtensionExecAggStage* e, MongoExtensionExecAggStage* src) noexcept {
        static_cast<Exec*>(e)->source = src; return ok();
    }
    static MongoExtensionStatus* open(MongoExtensionExecAggStage* e) noexcept {
        static_cast<Exec*>(e)->openCalls++; return ok();
    }
    static MongoExtensionStatus* reopen(MongoExtensionExecAggStage* e) noexcept {
        auto* x = static_cast<Exec*>(e); x->reopenCalls++; x->ready.clear(); x->eof = false; return ok();
    }
    static MongoExtensionStatus* close(MongoExtensionExecAggStage* e) noexcept {
        static_cast<Exec*>(e)->closeCalls++; return ok();
    }
    static MongoExtensionStatus* explain(const MongoExtensionExecAggStage* e, MongoExtensionQueryExecutionContext*,
                                         MongoExtensionExplainVerbosity, MongoExtensionByteBuf** out) noexcept {
        return guarded([&] {
            auto* x = static_cast<const Exec*>(e);
            mb::Writer w;
            w.i64("emitted", x->emitted); w.i64("dispatches", x->dispatches); w.i64("maxBatch", x->maxBatch);
            w.i64("sourcePulls", x->sourcePulls); w.i64("openCalls", x->openCalls); w.i64("closeCalls", x->closeCalls);
            w.boolean("helperLoaded", x->helperVolume != nullptr); w.string("probe", x->probeResult);
            w.i64("tickerTicks", x->ticks); w.boolean("workerUsed", x->workerUsed);
            w.boolean("workerOnOtherThread", x->workerUsed && x->workerThread != x->callerThread);
            *out = makeBuf(w.finish());
        });
    }
    static MongoExtensionStatus* getNext(MongoExtensionExecAggStage* e, MongoExtensionQueryExecutionContext* ctx,
                                         MongoExtensionGetNextResult* r) noexcept;
    static const MongoExtensionExecAggStageVTable kVT;

    // Returns nullptr for success, otherwise the status to hand back to the host (possibly the host's own status).
    MongoExtensionStatus* pollInterrupt(MongoExtensionQueryExecutionContext* ctx) {
        Status mine(MONGO_EXTENSION_STATUS_OK, "");  // extension-owned, on the stack: host only calls set_code/set_reason
        MongoExtensionStatus* rc = ctx->vtable->check_for_interrupt(ctx, &mine);
        if (rc->vtable->get_code(rc) != 0) return rc;  // call itself failed: pass the host status through
        rc->vtable->destroy(rc);
        if (mine.code != 0) return err(mine.code, mine.reason);
        return nullptr;
    }

    void lazyInit() {
        if (inited) return;
        inited = true;
        if (spec.ticker) tick = std::thread([this] { while (!stopTick) { ticks++; std::this_thread::sleep_for(std::chrono::milliseconds(10)); } });
        if (!spec.lib.empty()) {
            libHandle = dlopen(spec.lib.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!libHandle) throw MeshError(3, std::string("dlopen failed: ") + dlerror());
            helperVolume = reinterpret_cast<int (*)(const uint8_t*, uint64_t, double*)>(dlsym(libHandle, "mesh_helper_volume"));
            if (!helperVolume) throw MeshError(3, "dlsym mesh_helper_volume failed");
        }
        if (spec.probe) {
#ifdef __APPLE__
            const char* sys = "/usr/lib/libSystem.B.dylib";
#else
            const char* sys = "libc.so.6";
#endif
            void* h = dlopen(sys, RTLD_NOW | RTLD_LOCAL);
            auto fn = h ? reinterpret_cast<pid_t (*)()>(dlsym(h, "getpid")) : nullptr;
            probeResult = (fn && fn() == getpid()) ? "dlopen+dlsym ok" : "FAILED";
            if (h) dlclose(h);
        }
    }

    // Pull up to spec.batch documents from the host source. One doc per source get_next call.
    MongoExtensionStatus* fillBatch(MongoExtensionQueryExecutionContext* ctx, std::vector<Pending>& batch) {
        if (!source) throw MeshError(4, "no source set");
        while ((int)batch.size() < spec.batch && !eof) {
            MongoExtensionGetNextResult res{};
            res.code = kEOF;
            res.resultDocument = {kByteView, {}}; res.resultMetadata = {kByteView, {}};
            sourcePulls++;
            MongoExtensionStatus* s = source->vtable->get_next(source, ctx, &res);
            if (s->vtable->get_code(s) != 0) return s;  // pass the HOST's status through untouched (caller destroys it)
            s->vtable->destroy(s);
            auto take = [](MongoExtensionByteContainer& c, std::vector<uint8_t>& dst) {
                if (c.type == kByteBuf) {
                    if (c.bytes.buf) {
                        auto v = c.bytes.buf->vtable->get_view(c.bytes.buf);
                        dst.assign(v.data, v.data + v.len);
                        c.bytes.buf->vtable->destroy(c.bytes.buf);  // host-allocated: destroy through the HOST vtable
                        c.bytes.buf = nullptr;
                    }
                } else if (c.bytes.view.data && c.bytes.view.len) {
                    dst.assign(c.bytes.view.data, c.bytes.view.data + c.bytes.view.len);  // view dies at next source get_next
                }
            };
            if (res.code == kAdvanced) {
                Pending p; take(res.resultDocument, p.doc); take(res.resultMetadata, p.meta);
                if (p.doc.empty()) throw MeshError(5, "source returned kAdvanced with empty document");
                batch.push_back(std::move(p));
            } else {
                std::vector<uint8_t> discard;  // a buf transferred on a non-advanced path must still be destroyed
                take(res.resultDocument, discard); take(res.resultMetadata, discard);
                if (res.code == kEOF) eof = true;
                if (res.code == kPauseExecution) break;
            }
        }
        return nullptr;
    }

    double measure(const uint8_t* p, size_t n) {
        double v = 0; std::string e;
        if (helperVolume) { if (helperVolume(p, n, &v) != 0) throw MeshError(2, "invalid mesh payload (helper)"); return v; }
        if (!meshfmt::volume(p, n, v, e)) throw MeshError(2, "invalid mesh payload: " + e);
        return v;
    }

    // One "dispatch" over the whole batch.
    void computeBatch(std::vector<Pending>& batch) {
        auto body = [&] {
            for (auto& p : batch) {
                mb::Doc d{p.doc.data(), p.doc.size()};
                if (!d.valid()) throw MeshError(2, "input is not a valid BSON document");
                mb::Elem f;
                if (!d.find(spec.field.c_str(), f)) throw MeshError(2, "document has no field '" + spec.field + "'");
                const uint8_t* data; size_t len; uint8_t sub;
                if (!mb::bin(f, data, len, sub) || sub != 0) throw MeshError(2, "field '" + spec.field + "' is not BinData subtype 0");
                double vol = measure(data, len);
                mb::Writer w; size_t c = 0; mb::Elem el;
                while (d.next(c, el)) if (spec.as != el.name) w.element(el);
                w.dbl(spec.as.c_str(), vol);
                p.doc = w.finish();
            }
        };
        if (!spec.worker) { body(); return; }
        // Extension-owned thread. An exception must never escape the thread (std::terminate): capture and rethrow.
        std::exception_ptr ep;
        std::thread t([&] { workerThread = std::this_thread::get_id(); try { body(); } catch (...) { ep = std::current_exception(); } });
        workerUsed = true;
        t.join();
        if (ep) std::rethrow_exception(ep);
    }

    MongoExtensionStatus* sleepPhase(MongoExtensionQueryExecutionContext* ctx) {
        if (spec.mode == "slow") {  // does NOT look at the deadline: shows the host cannot preempt the extension
            std::this_thread::sleep_for(std::chrono::milliseconds(spec.sleepMs));
            return nullptr;
        }
        // slow_poll: sleeps in 20 ms slices, polling check_for_interrupt AND the explicit deadline.
        int64_t deadline = INT64_MAX;
        MongoExtensionStatus* ds = ctx->vtable->get_deadline_timestamp_ms(ctx, &deadline);
        if (ds->vtable->get_code(ds) != 0) return ds;
        ds->vtable->destroy(ds);
        for (int slept = 0; slept < spec.sleepMs; slept += 20) {
            if (auto* s = pollInterrupt(ctx)) return s;
            if (deadline != INT64_MAX && nowMs() > deadline) return err(50, "$meshMeasure: operation exceeded its deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return nullptr;
    }

    MongoExtensionStatus* doGetNext(MongoExtensionQueryExecutionContext* ctx, MongoExtensionGetNextResult* r) {
        callerThread = std::this_thread::get_id();
        r->code = kEOF; r->resultDocument = {kByteView, {}}; r->resultMetadata = {kByteView, {}};
        if (spec.mode == "throw") throw std::runtime_error("planted fault inside $meshMeasure (mode=throw)");
        if (spec.mode == "code") throw MeshError(11601, "planted uassert-style error (mode=code)");
        lazyInit();
        if (ready.empty() && !eof) {
            if (auto* s = pollInterrupt(ctx)) return s;  // once per batch
            std::vector<Pending> batch;
            if (auto* s = fillBatch(ctx, batch)) return s;
            if (!batch.empty()) {
                if (spec.mode == "slow" || spec.mode == "slow_poll") if (auto* s = sleepPhase(ctx)) return s;
                computeBatch(batch);
                dispatches++; maxBatch = std::max<long>(maxBatch, (long)batch.size());
                for (auto& p : batch) ready.push_back(std::move(p));
            }
        }
        if (ready.empty()) { r->code = eof ? kEOF : kPauseExecution; return nullptr; }
        Pending p = std::move(ready.front()); ready.pop_front();
        bool asView = spec.out == "view" || (spec.out == "alt" && (emitted % 2 == 1));
        if (asView) {
            lastOut = std::move(p.doc);  // stays valid until the next get_next / destroy
            r->resultDocument = {kByteView, {}}; r->resultDocument.bytes.view = view(lastOut);
        } else {
            r->resultDocument.type = kByteBuf; r->resultDocument.bytes.buf = makeBuf(std::move(p.doc));
        }
        if (!p.meta.empty()) { r->resultMetadata.type = kByteBuf; r->resultMetadata.bytes.buf = makeBuf(std::move(p.meta)); }
        r->code = kAdvanced; emitted++;
        return nullptr;
    }
};

MongoExtensionStatus* Exec::getNext(MongoExtensionExecAggStage* e, MongoExtensionQueryExecutionContext* ctx,
                                    MongoExtensionGetNextResult* r) noexcept {
    auto* x = static_cast<Exec*>(e);
    // Deliberately outside the guard: demonstrates what happens when an exception crosses the noexcept C boundary.
    if (x->spec.mode == "escape") throw std::runtime_error("escaping exception (mode=escape)");
    MongoExtensionStatus* passthrough = nullptr;
    MongoExtensionStatus* st = guarded([&] { passthrough = x->doGetNext(ctx, r); });
    if (passthrough) {  // an error status produced by doGetNext (ours or the host's)
        if (st != ok()) st->vtable->destroy(st);
        return passthrough;
    }
    return st;
}
const MongoExtensionExecAggStageVTable Exec::kVT{&Exec::destroy, &Exec::getNext, &Exec::getName, &Exec::createMetrics,
                                                  &Exec::setSource, &Exec::open, &Exec::reopen, &Exec::close, &Exec::explain};

// ---------------------------------------------------------------- LogicalAggStage
struct Logical : MongoExtensionLogicalAggStage {
    Spec spec;
    std::string ns;  // copied out of the catalog context (views are only valid during promote())
    explicit Logical(Spec s, std::string n) : MongoExtensionLogicalAggStage{&kVT}, spec(std::move(s)), ns(std::move(n)) { gAllocs++; }
    static Logical* self(const MongoExtensionLogicalAggStage* l) { return const_cast<Logical*>(static_cast<const Logical*>(l)); }
    static void destroy(MongoExtensionLogicalAggStage* l) noexcept { delete static_cast<Logical*>(l); gFrees++; }
    static MongoExtensionByteView getName(const MongoExtensionLogicalAggStage*) noexcept { return view("$meshMeasure", 12); }
    static MongoExtensionStatus* serialize(const MongoExtensionLogicalAggStage* l, MongoExtensionByteBuf** out) noexcept {
        return guarded([&] { *out = makeBuf(self(l)->spec.bson); });
    }
    static MongoExtensionStatus* explain(const MongoExtensionLogicalAggStage* l, MongoExtensionQueryExecutionContext*,
                                         MongoExtensionExplainVerbosity, MongoExtensionByteBuf** out) noexcept {
        return guarded([&] {
            mb::Writer w; size_t o = w.begin("$meshMeasure");
            w.string("field", self(l)->spec.field); w.string("as", self(l)->spec.as); w.i32("batch", self(l)->spec.batch);
            w.string("ns", self(l)->ns);
            w.end(o); *out = makeBuf(w.finish());
        });
    }
    static MongoExtensionStatus* compile(const MongoExtensionLogicalAggStage* l, MongoExtensionExecAggStage** out) noexcept {
        return guarded([&] { *out = new Exec(self(l)->spec); });
    }
    static MongoExtensionStatus* getDpl(const MongoExtensionLogicalAggStage*, MongoExtensionDistributedPlanLogic** out) noexcept {
        *out = nullptr;  // "can run fully in parallel on the shards" (api.h get_distributed_plan_logic)
        return ok();
    }
    static MongoExtensionStatus* clone(const MongoExtensionLogicalAggStage* l, MongoExtensionLogicalAggStage** out) noexcept {
        return guarded([&] { *out = new Logical(self(l)->spec, self(l)->ns); });
    }
    static MongoExtensionStatus* vsLimit(MongoExtensionLogicalAggStage*, long long*) noexcept { return ok(); }
    static MongoExtensionStatus* rulePre(const MongoExtensionLogicalAggStage*, MongoExtensionByteView,
                                         const MongoExtensionPipelineRewriteContext*, bool* r) noexcept { *r = false; return ok(); }
    static MongoExtensionStatus* ruleXform(MongoExtensionLogicalAggStage*, MongoExtensionByteView,
                                           MongoExtensionPipelineRewriteContext*, bool* r) noexcept { *r = false; return ok(); }
    static MongoExtensionStatus* getFilter(const MongoExtensionLogicalAggStage*, MongoExtensionByteBuf** out) noexcept {
        *out = nullptr; return ok();
    }
    static MongoExtensionStatus* applyDeps(MongoExtensionLogicalAggStage*, const MongoExtensionPipelineDependencies*) noexcept { return ok(); }
    static MongoExtensionStatus* getSort(const MongoExtensionLogicalAggStage*, MongoExtensionByteBuf** out) noexcept {
        *out = nullptr; return ok();
    }
    static MongoExtensionStatus* skipStream(MongoExtensionLogicalAggStage*, MongoExtensionStreamType) noexcept { return ok(); }
    static MongoExtensionStatus* docsNeeded(const MongoExtensionLogicalAggStage*, MongoExtensionByteBuf** out) noexcept {
        return guarded([&] { mb::Writer w; w.string("effect", "noEffect"); *out = makeBuf(w.finish()); });
    }
    static const MongoExtensionLogicalAggStageVTable kVT;
};
const MongoExtensionLogicalAggStageVTable Logical::kVT{
    &Logical::destroy, &Logical::getName, &Logical::serialize, &Logical::explain, &Logical::compile, &Logical::getDpl,
    &Logical::clone, &Logical::vsLimit, &Logical::rulePre, &Logical::ruleXform, &Logical::getFilter, &Logical::applyDeps,
    &Logical::getSort, &Logical::skipStream, &Logical::docsNeeded};

// ---------------------------------------------------------------- AstNode
struct Ast : MongoExtensionAggStageAstNode {
    Spec spec;
    explicit Ast(Spec s) : MongoExtensionAggStageAstNode{&kVT}, spec(std::move(s)) { gAllocs++; }
    static Ast* self(const MongoExtensionAggStageAstNode* a) { return const_cast<Ast*>(static_cast<const Ast*>(a)); }
    static void destroy(MongoExtensionAggStageAstNode* a) noexcept { delete static_cast<Ast*>(a); gFrees++; }
    static MongoExtensionByteView getName(const MongoExtensionAggStageAstNode*) noexcept { return view("$meshMeasure", 12); }
    // Static properties: public/extension_agg_stage_static_properties.idl (MongoExtensionStaticProperties).
    static MongoExtensionStatus* getProperties(const MongoExtensionAggStageAstNode*, MongoExtensionByteBuf** out) noexcept {
        return guarded([&] {
            mb::Writer w;
            w.string("streamType", "streaming");     // one-in-one-out, no blocking
            w.string("position", "none");
            w.string("hostType", "none");
            w.boolean("requiresInputDocSource", true);  // transform stage
            size_t privs = w.begin("requiredPrivileges", true);
            size_t p0 = w.begin("0");
            w.string("resourcePattern", "namespace");
            size_t acts = w.begin("actions", true);
            size_t a0 = w.begin("0"); w.string("action", "find"); w.end(a0);
            w.end(acts); w.end(p0); w.end(privs);
            w.boolean("preservesUpstreamMetadata", true);
            w.boolean("allowedInUnionWith", true);
            w.boolean("allowedInLookup", true);
            w.boolean("allowedInFacet", true);
            w.boolean("isSelectionStage", false);
            *out = makeBuf(w.finish());
        });
    }
    static MongoExtensionStatus* promote(const MongoExtensionAggStageAstNode* a, const MongoExtensionCatalogContext* c,
                                         MongoExtensionLogicalAggStage** out) noexcept {
        return guarded([&] {
            std::string ns = toString(c->namespaceString.databaseName) + "." + toString(c->namespaceString.collectionName);
            *out = new Logical(self(a)->spec, ns);
        });
    }
    static MongoExtensionStatus* clone(const MongoExtensionAggStageAstNode* a, MongoExtensionAggStageAstNode** out) noexcept {
        return guarded([&] { *out = new Ast(self(a)->spec); });
    }
    static MongoExtensionStatus* viewPolicy(const MongoExtensionAggStageAstNode*, MongoExtensionFirstStageViewApplicationPolicy* o) noexcept {
        *o = kDefaultPrepend; return ok();
    }
    static MongoExtensionStatus* bindNs(MongoExtensionAggStageAstNode*, const MongoExtensionResolvedNamespace*) noexcept { return ok(); }
    static const MongoExtensionAggStageAstNodeVTable kVT;
};
const MongoExtensionAggStageAstNodeVTable Ast::kVT{&Ast::destroy, &Ast::getName, &Ast::getProperties, &Ast::promote,
                                                    &Ast::clone, &Ast::viewPolicy, &Ast::bindNs};

// ---------------------------------------------------------------- ParseNode (+ expanded-array container)
struct Expanded : MongoExtensionExpandedArrayContainer {
    Spec spec;
    explicit Expanded(Spec s) : MongoExtensionExpandedArrayContainer{&kVT}, spec(std::move(s)) { gAllocs++; }
    static void destroy(MongoExtensionExpandedArrayContainer* c) noexcept { delete static_cast<Expanded*>(c); gFrees++; }
    static size_t size(const MongoExtensionExpandedArrayContainer*) noexcept { return 1; }
    static MongoExtensionStatus* transfer(MongoExtensionExpandedArrayContainer* c, MongoExtensionExpandedArray* arr) noexcept {
        return guarded([&] {
            if (arr->size != 1) throw MeshError(6, "expanded array must be pre-sized to size()");
            arr->elements[0].type = kAstNode;
            arr->elements[0].parseOrAst.ast = new Ast(static_cast<Expanded*>(c)->spec);
        });
    }
    static const MongoExtensionExpandedArrayContainerVTable kVT;
};
const MongoExtensionExpandedArrayContainerVTable Expanded::kVT{&Expanded::destroy, &Expanded::size, &Expanded::transfer};

struct Parse : MongoExtensionAggStageParseNode {
    Spec spec;
    explicit Parse(Spec s) : MongoExtensionAggStageParseNode{&kVT}, spec(std::move(s)) { gAllocs++; }
    static Parse* self(const MongoExtensionAggStageParseNode* p) { return const_cast<Parse*>(static_cast<const Parse*>(p)); }
    static void destroy(MongoExtensionAggStageParseNode* p) noexcept { delete static_cast<Parse*>(p); gFrees++; }
    static MongoExtensionByteView getName(const MongoExtensionAggStageParseNode*) noexcept { return view("$meshMeasure", 12); }
    static MongoExtensionStatus* queryShape(const MongoExtensionAggStageParseNode* p, const MongoExtensionHostQueryShapeOpts* o,
                                            MongoExtensionByteBuf** out) noexcept {
        return guarded([&] {
            // Field paths must be run through the host's serializer (so they can be redacted); literals become "?".
            auto ser = [&](const std::string& path) {
                MongoExtensionByteBuf* b = nullptr;
                MongoExtensionStatus* s = o->vtable->serialize_field_path(o, view(path), &b);
                if (s->vtable->get_code(s) != 0) {
                    std::string why = toString(s->vtable->get_reason(s)); int32_t code = s->vtable->get_code(s);
                    s->vtable->destroy(s); throw MeshError(code, why);
                }
                s->vtable->destroy(s);
                auto v = b->vtable->get_view(b); std::string res = toString(v);
                b->vtable->destroy(b);  // host-allocated: destroy via host vtable
                return res;
            };
            mb::Writer w; size_t d = w.begin("$meshMeasure");
            w.string("field", ser(self(p)->spec.field)); w.string("as", ser(self(p)->spec.as)); w.string("batch", "?number");
            w.end(d); *out = makeBuf(w.finish());
        });
    }
    static MongoExtensionStatus* expand(const MongoExtensionAggStageParseNode* p, MongoExtensionExpandedArrayContainer** out) noexcept {
        return guarded([&] { *out = new Expanded(self(p)->spec); });
    }
    static MongoExtensionStatus* clone(const MongoExtensionAggStageParseNode* p, MongoExtensionAggStageParseNode** out) noexcept {
        return guarded([&] { *out = new Parse(self(p)->spec); });
    }
    static MongoExtensionStatus* toBsonForLog(const MongoExtensionAggStageParseNode* p, MongoExtensionByteBuf** out) noexcept {
        return guarded([&] { *out = makeBuf(self(p)->spec.bson); });
    }
    static const MongoExtensionAggStageParseNodeVTable kVT;
};
const MongoExtensionAggStageParseNodeVTable Parse::kVT{&Parse::destroy, &Parse::getName, &Parse::queryShape, &Parse::expand,
                                                        &Parse::clone, &Parse::toBsonForLog};

// ---------------------------------------------------------------- Descriptor (static, extension-owned, never freed)
struct Descriptor : MongoExtensionAggStageDescriptor {
    Descriptor() : MongoExtensionAggStageDescriptor{&kVT} {}
    static MongoExtensionByteView getName(const MongoExtensionAggStageDescriptor*) noexcept { return view("$meshMeasure", 12); }
    static MongoExtensionClientType clientType(const MongoExtensionAggStageDescriptor*) noexcept { return kMongoExtensionClientTypeAny; }
    static MongoExtensionStatus* parse(const MongoExtensionAggStageDescriptor*, MongoExtensionByteView stageBson,
                                       MongoExtensionAggStageParseNode** out) noexcept {
        return guarded([&] { *out = new Parse(parseSpec(stageBson)); });
    }
    static const MongoExtensionAggStageDescriptorVTable kVT;
};
const MongoExtensionAggStageDescriptorVTable Descriptor::kVT{&Descriptor::getName, &Descriptor::clientType, &Descriptor::parse};
Descriptor gDescriptor;

// ---------------------------------------------------------------- Extension object
MongoExtensionStatus* initialize(const MongoExtension*, const MongoExtensionHostPortal* portal) noexcept {
    return guarded([&] {
        // Extension options arrive as a YAML text (sdk/host_portal.h:75 parses them with YAML::Load). Poor man's parse.
        std::string opts = toString(portal->vtable->get_extension_options(portal));
        auto pos = opts.find("defaultBatch:");
        if (pos != std::string::npos) gDefaultBatch = std::max(1, atoi(opts.c_str() + pos + 13));
        MongoExtensionStatus* s = portal->vtable->register_stage_descriptor(portal, &gDescriptor);
        if (s->vtable->get_code(s) != 0) {
            int32_t code = s->vtable->get_code(s); std::string why = toString(s->vtable->get_reason(s));
            s->vtable->destroy(s);
            throw MeshError(code, "register_stage_descriptor failed: " + why);
        }
        s->vtable->destroy(s);
    });
}
const MongoExtensionVTable kExtVTable{&initialize};
MongoExtension gExtension{&kExtVTable, {0, 0}};
const MongoExtensionAPIVersion kVersions[] = {{MONGODB_EXTENSION_API_MAJOR_VERSION, MONGODB_EXTENSION_API_MINOR_VERSION}};

}  // namespace

// ================================================================ exported entry points
// Phase 1 (api_contract.h): static storage, no exceptions possible.
EXPORT void get_mongodb_extension_versions(MongoExtensionAPIVersionVector* v) {
    v->len = 1;
    v->versions = kVersions;
}

// Phase 2 (api.h).
EXPORT MongoExtensionStatus* get_mongodb_extension(MongoExtensionAPIVersion version, const MongoExtensionHostServices* hostServices,
                                                   const MongoExtension** extension) {
    return guarded([&] {
        if (version.major != MONGODB_EXTENSION_API_MAJOR_VERSION || version.minor > MONGODB_EXTENSION_API_MINOR_VERSION)
            throw MeshError(2, "unsupported API version " + std::to_string(version.major) + "." + std::to_string(version.minor));
        gHostServices = hostServices;
        gExtension.version = version;  // must equal the negotiated version (host uassert 13693501)
        *extension = &gExtension;
    });
}

#ifdef MESH_TEST_HOOKS
EXPORT void mesh_ext_test_counters(long* allocs, long* frees) { *allocs = gAllocs; *frees = gFrees; }
#endif
