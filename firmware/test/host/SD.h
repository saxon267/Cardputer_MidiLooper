#pragma once
// Minimal host-side SD shim used by test_store.cpp.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

#define FILE_READ  0
#define FILE_WRITE 1

extern std::vector<uint8_t> g_sd_buf;

class File {
public:
    File() = default;
    explicit File(bool open) : _open(open) {}

    size_t write(const void* data, size_t n)
    {
        if (!_open) return 0;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        g_sd_buf.insert(g_sd_buf.end(), p, p + n);
        return n;
    }

    size_t read(void* dst, size_t n)
    {
        if (!_open || _pos >= g_sd_buf.size()) return 0;
        size_t k = g_sd_buf.size() - _pos;
        if (k > n) k = n;
        memcpy(dst, &g_sd_buf[_pos], k);
        _pos += k;
        return k;
    }

    size_t size() const { return g_sd_buf.size(); }
    void close() { _open = false; }
    operator bool() const { return _open; }

private:
    bool   _open = false;
    size_t _pos  = 0;
};

class SDClass {
public:
    bool mkdir(const char*) { return true; }
    bool exists(const char*) { return false; }
    File open(const char*, int mode = FILE_READ)
    {
        if (mode == FILE_WRITE) g_sd_buf.clear();
        return File(true);
    }
};

extern SDClass SD;
