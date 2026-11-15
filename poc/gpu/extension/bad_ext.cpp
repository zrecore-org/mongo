// MP14a POC: planted bad extensions for bar 1, selected with -DBAD_VARIANT=n. Public C headers only.
#include "mongo/db/extension/public/api.h"
#include <cstdint>
#include <cstdlib>

#define EXPORT __attribute__((visibility("default")))
static const MongoExtensionAPIVersion v20[] = {{2, 0}};
static const MongoExtensionAPIVersion v11[] = {{1, 1}};
static const MongoExtensionAPIVersion v10[] = {{1, 0}};
static const MongoExtensionAPIVersion vmax[] = {{UINT32_MAX, 0}};
static MongoExtensionStatus* initFail(const MongoExtension*, const MongoExtensionHostPortal*);
static const MongoExtensionVTable vtFail = {&initFail};
static const MongoExtension extV2 = {&vtFail, {2, 0}};
static const MongoExtension extV1 = {&vtFail, {1, 0}};

struct St : MongoExtensionStatus {
    static const MongoExtensionStatusVTable vt;
    int32_t code;
    const char* reason;
    St(int32_t c, const char* r) : MongoExtensionStatus{&vt}, code(c), reason(r) {}
};
static MongoExtensionStatus* stClone(const MongoExtensionStatus* s, MongoExtensionStatus** out) { *out = const_cast<MongoExtensionStatus*>(s); return *out; }
const MongoExtensionStatusVTable St::vt = {
    [](MongoExtensionStatus*) {},
    [](const MongoExtensionStatus* s) -> int32_t { return static_cast<const St*>(s)->code; },
    [](const MongoExtensionStatus* s) -> MongoExtensionByteView {
        const char* r = static_cast<const St*>(s)->reason;
        size_t n = 0; while (r[n]) n++;
        return {reinterpret_cast<const uint8_t*>(r), n};
    },
    [](MongoExtensionStatus*, int32_t) {},
    [](MongoExtensionStatus* s, MongoExtensionByteView) -> MongoExtensionStatus* { return s; },
    &stClone,
};
static St errStatus(7, "planted initialize failure");
static St okStatus(0, "");
static MongoExtensionStatus* initFail(const MongoExtension*, const MongoExtensionHostPortal*) { return &errStatus; }

extern "C" {
EXPORT void get_mongodb_extension_versions(MongoExtensionAPIVersionVector* out) {
#if BAD_VARIANT == 1
    out->len = 1; out->versions = v20;          // only a foreign major
#elif BAD_VARIANT == 2
    out->len = 1; out->versions = v11;          // minor above the host's
#elif BAD_VARIANT == 3
    out->len = 0; out->versions = nullptr;      // publishes nothing
#elif BAD_VARIANT == 7
    out->len = 1; out->versions = vmax;         // major = UINT32_MAX
#else
    out->len = 1; out->versions = v10;
#endif
}
#if BAD_VARIANT != 5
EXPORT MongoExtensionStatus* get_mongodb_extension(MongoExtensionAPIVersion, const MongoExtensionHostServices*, const MongoExtension** out) {
#if BAD_VARIANT == 4
    *out = &extV2;                              // advertises 1.0 but returns an extension claiming 2.0
#else
    *out = &extV1;                              // variant 6: initialize() fails
#endif
    return &okStatus;
}
#endif
}
