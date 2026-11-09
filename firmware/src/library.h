#pragma once
#include <stdint.h>
#include "config.h"

struct FileEntry {
    char     path[96] = {0};
    char     name[40] = {0};
    uint32_t size     = 0;
    uint16_t durSec   = 0;   // 0 = not probed yet
    bool     dir      = false;
};

class Library {
public:
    bool begin();
    bool mounted() const { return _mounted; }
    bool scan(const char* path);
    bool enter(int index);
    bool up();

    int               count() const { return _n; }
    const FileEntry&  at(int i) const { return _e[i]; }
    const char*       path() const { return _path; }
    const char*       cardName() const { return _card; }
    void              setDuration(int i, uint16_t sec) { if (i >= 0 && i < _n) _e[i].durSec = sec; }

private:
    FileEntry _e[MAX_FILES];
    int       _n       = 0;
    bool      _mounted = false;
    char      _path[96] = LOOP_DIR;
    char      _card[16] = "NONE";
};

extern Library library;
