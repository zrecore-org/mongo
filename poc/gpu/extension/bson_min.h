// MP14a POC. Minimal bounds-checked BSON reader/writer (no mongo/base dependency), shared by the extension and the fake host.
// Only the element types the POC needs are supported; anything else makes validation fail (clean error, never a crash).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace bm {
enum : uint8_t { kDouble = 1, kString = 2, kDoc = 3, kArray = 4, kBinData = 5, kOid = 7, kBool = 8, kDate = 9, kNull = 10, kInt32 = 16, kTimestamp = 17, kInt64 = 18, kDecimal = 19 };

struct Elem {
    uint8_t type = 0;
    std::string_view name;
    const uint8_t* val = nullptr;  // start of the value bytes
    size_t vlen = 0;               // value length in bytes
    const uint8_t* start = nullptr, *end = nullptr;  // whole element
};

inline bool rd32(const uint8_t* p, const uint8_t* end, int32_t* out) {
    if (end - p < 4) return false;
    std::memcpy(out, p, 4);
    return true;
}

// Reads the element at p (p < docEnd - 1, i.e. not the terminating 0). Returns false if malformed.
inline bool readElem(const uint8_t* p, const uint8_t* docEnd, Elem* e) {
    if (p >= docEnd) return false;
    e->start = p;
    e->type = *p++;
    const uint8_t* nul = static_cast<const uint8_t*>(std::memchr(p, 0, docEnd - p));
    if (!nul) return false;
    e->name = std::string_view(reinterpret_cast<const char*>(p), nul - p);
    p = nul + 1;
    e->val = p;
    int32_t n = 0;
    size_t len = 0;
    switch (e->type) {
        case kDouble: case kDate: case kTimestamp: case kInt64: len = 8; break;
        case kInt32: len = 4; break;
        case kOid: len = 12; break;
        case kDecimal: len = 16; break;
        case kBool: len = 1; break;
        case kNull: len = 0; break;
        case kString:
            if (!rd32(p, docEnd, &n) || n < 1 || (int64_t)n > (docEnd - p) - 4) return false;
            if (p[4 + n - 1] != 0) return false;
            len = 4 + (size_t)n;
            break;
        case kDoc: case kArray:
            if (!rd32(p, docEnd, &n) || n < 5 || (int64_t)n > docEnd - p) return false;
            if (p[n - 1] != 0) return false;
            len = (size_t)n;
            break;
        case kBinData:
            if (!rd32(p, docEnd, &n) || n < 0 || (int64_t)n > (docEnd - p) - 5) return false;
            len = 5 + (size_t)n;
            break;
        default: return false;
    }
    if ((int64_t)len > docEnd - p) return false;
    e->vlen = len;
    e->end = p + len;
    return true;
}

// Full structural validation of a top-level document (length, terminator, every element, no trailing bytes).
inline bool validateDoc(const uint8_t* d, size_t len) {
    int32_t n;
    if (len < 5 || !rd32(d, d + len, &n) || n < 5 || (size_t)n != len || d[len - 1] != 0) return false;
    const uint8_t* p = d + 4;
    const uint8_t* end = d + len - 1;
    while (p < end) {
        Elem e;
        if (!readElem(p, end, &e)) return false;
        p = e.end;
    }
    return p == end;
}

// Finds a top-level field (doc must already be validated).
inline bool findField(const uint8_t* d, size_t len, std::string_view name, Elem* out) {
    const uint8_t* p = d + 4;
    const uint8_t* end = d + len - 1;
    while (p < end) {
        Elem e;
        if (!readElem(p, end, &e)) return false;
        if (e.name == name) { *out = e; return true; }
        p = e.end;
    }
    return false;
}

struct Builder {
    std::string b;
    Builder() { b.assign(4, '\0'); }
    void raw(const void* p, size_t n) { b.append(static_cast<const char*>(p), n); }
    void head(uint8_t t, std::string_view name) { b.push_back((char)t); b.append(name); b.push_back('\0'); }
    Builder& dbl(std::string_view n, double v) { head(kDouble, n); raw(&v, 8); return *this; }
    Builder& i32(std::string_view n, int32_t v) { head(kInt32, n); raw(&v, 4); return *this; }
    Builder& i64(std::string_view n, int64_t v) { head(kInt64, n); raw(&v, 8); return *this; }
    Builder& boolean(std::string_view n, bool v) { head(kBool, n); b.push_back(v ? 1 : 0); return *this; }
    Builder& str(std::string_view n, std::string_view v) { head(kString, n); int32_t l = (int32_t)v.size() + 1; raw(&l, 4); b.append(v); b.push_back('\0'); return *this; }
    Builder& bin(std::string_view n, const void* p, size_t len) { head(kBinData, n); int32_t l = (int32_t)len; raw(&l, 4); b.push_back(0); raw(p, len); return *this; }
    Builder& doc(std::string_view n, const std::string& finished) { head(kDoc, n); b.append(finished); return *this; }
    Builder& copyElem(const Elem& e) { b.append(reinterpret_cast<const char*>(e.start), e.end - e.start); return *this; }
    std::string finish() { std::string r = b; r.push_back('\0'); int32_t l = (int32_t)r.size(); std::memcpy(&r[0], &l, 4); return r; }
};
}  // namespace bm
