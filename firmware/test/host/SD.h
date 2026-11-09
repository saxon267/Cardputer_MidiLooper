#pragma once
// Minimal host-side SD shim used by test_store.cpp.
#include <stdint.h>
#include <stddef.h>
#include <vector>

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
    void close() { _open = false; }
    operator bool() const { return _open; }

private:
    bool _open = false;
};

class SDClass {
public:
    bool mkdir(const char*) { return true; }
    bool exists(const char*) { return false; }
    File open(const char*, int = 0)
    {
        g_sd_buf.clear();
        return File(true);
    }
};

extern SDClass SD;
