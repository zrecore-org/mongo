// Minimal BSON reader/writer for the MP14a POC. Std-library only: NO mongo/base, NO server headers.
// Covers the types this POC needs: double(1) string(2) document(3) array(4) binData(5) bool(8) int32(16) int64(18);
// other types are skippable (so documents can be copied element by element).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mb {

struct Elem {
    uint8_t type = 0;
    const char* name = nullptr;
    const uint8_t* val = nullptr;  // start of value bytes
    size_t valLen = 0;             // byte length of the value
    const uint8_t* raw = nullptr;  // start of the whole element (type byte)
    size_t rawLen = 0;
};

inline bool valueSize(uint8_t t, const uint8_t* v, const uint8_t* end, size_t& out) {
    auto rd32 = [&](int32_t& x) { if (end - v < 4) return false; memcpy(&x, v, 4); return true; };
    int32_t n = 0;
    switch (t) {
        case 0x01: case 0x09: case 0x11: case 0x12: out = 8; break;
        case 0x02: if (!rd32(n) || n < 1) return false; out = 4 + (size_t)n; break;
        case 0x03: case 0x04: if (!rd32(n) || n < 5) return false; out = (size_t)n; break;
        case 0x05: if (!rd32(n) || n < 0) return false; out = 5 + (size_t)n; break;
        case 0x07: out = 12; break;
        case 0x08: out = 1; break;
        case 0x0A: out = 0; break;
        case 0x10: out = 4; break;
        case 0x13: out = 16; break;
        default: return false;
    }
    return (size_t)(end - v) >= out;
}

struct Doc {
    const uint8_t* p = nullptr;
    size_t n = 0;
    bool valid() const {
        if (!p || n < 5) return false;
        int32_t len; memcpy(&len, p, 4);
        return len >= 5 && (size_t)len <= n && p[len - 1] == 0;
    }
    size_t size() const { int32_t len; memcpy(&len, p, 4); return (size_t)len; }
    // Iterate. cursor starts at 0.
    bool next(size_t& cursor, Elem& e) const {
        if (cursor == 0) cursor = 4;
        const uint8_t* end = p + size();
        const uint8_t* q = p + cursor;
        if (q >= end - 1) return false;  // reached trailing 0
        e.raw = q;
        e.type = *q++;
        e.name = reinterpret_cast<const char*>(q);
        while (q < end && *q) ++q;
        if (q >= end) return false;
        ++q;  // skip NUL
        e.val = q;
        if (!valueSize(e.type, q, end, e.valLen)) return false;
        e.rawLen = (size_t)(q + e.valLen - e.raw);
        cursor = (size_t)(q + e.valLen - p);
        return true;
    }
    bool find(const char* name, Elem& out) const {
        size_t c = 0; Elem e;
        while (next(c, e)) if (!strcmp(e.name, name)) { out = e; return true; }
        return false;
    }
};

inline Doc subdoc(const Elem& e) { return Doc{e.val, e.valLen}; }
inline std::string str(const Elem& e) {  // type 2
    int32_t n; memcpy(&n, e.val, 4);
    return std::string(reinterpret_cast<const char*>(e.val + 4), (size_t)(n - 1));
}
inline bool num(const Elem& e, double& d) {
    if (e.type == 0x01) { memcpy(&d, e.val, 8); return true; }
    if (e.type == 0x10) { int32_t x; memcpy(&x, e.val, 4); d = x; return true; }
    if (e.type == 0x12) { int64_t x; memcpy(&x, e.val, 8); d = (double)x; return true; }
    return false;
}
inline bool boolean(const Elem& e) { return e.val[0] != 0; }
// binData payload (type 5): returns pointer to bytes and length, and subtype.
inline bool bin(const Elem& e, const uint8_t*& data, size_t& len, uint8_t& sub) {
    if (e.type != 0x05) return false;
    int32_t n; memcpy(&n, e.val, 4);
    sub = e.val[4]; data = e.val + 5; len = (size_t)n; return true;
}

class Writer {
public:
    Writer() { buf_.resize(4); }
    void raw(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    void head(uint8_t t, const char* name) { buf_.push_back(t); raw((const uint8_t*)name, strlen(name) + 1); }
    void dbl(const char* name, double v) { head(0x01, name); raw((const uint8_t*)&v, 8); }
    void i32(const char* name, int32_t v) { head(0x10, name); raw((const uint8_t*)&v, 4); }
    void i64(const char* name, int64_t v) { head(0x12, name); raw((const uint8_t*)&v, 8); }
    void boolean(const char* name, bool v) { head(0x08, name); buf_.push_back(v ? 1 : 0); }
    void string(const char* name, const std::string& s) {
        head(0x02, name); int32_t n = (int32_t)s.size() + 1; raw((const uint8_t*)&n, 4);
        raw((const uint8_t*)s.c_str(), s.size() + 1);
    }
    void binary(const char* name, const uint8_t* d, size_t n, uint8_t sub = 0) {
        head(0x05, name); int32_t l = (int32_t)n; raw((const uint8_t*)&l, 4); buf_.push_back(sub); raw(d, n);
    }
    void element(const Elem& e) { raw(e.raw, e.rawLen); }
    void doc(const char* name, const std::vector<uint8_t>& d) { head(0x03, name); raw(d.data(), d.size()); }
    size_t begin(const char* name, bool array = false) {
        head(array ? 0x04 : 0x03, name); size_t off = buf_.size(); buf_.resize(off + 4); return off;
    }
    void end(size_t off) { buf_.push_back(0); int32_t l = (int32_t)(buf_.size() - off); memcpy(&buf_[off], &l, 4); }
    std::vector<uint8_t> finish() {
        buf_.push_back(0); int32_t l = (int32_t)buf_.size(); memcpy(&buf_[0], &l, 4);
        return std::move(buf_);
    }
private:
    std::vector<uint8_t> buf_;
};

}  // namespace mb
