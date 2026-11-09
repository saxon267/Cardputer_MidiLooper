#include "library.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>
#include <strings.h>

Library library;

static SPIClass sdSPI;

static bool isMidiName(const char* n)
{
    const char* dot = strrchr(n, '.');
    if (!dot) return false;
    return strcasecmp(dot, ".mid") == 0 || strcasecmp(dot, ".midi") == 0;
}

static const char* baseName(const char* n)
{
    const char* s = strrchr(n, '/');
    return s ? s + 1 : n;
}

static int entryCmp(const void* a, const void* b)
{
    const FileEntry* x = (const FileEntry*)a;
    const FileEntry* y = (const FileEntry*)b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

bool Library::begin()
{
    sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    _mounted = SD.begin(PIN_SD_CS, sdSPI, 20000000);
    if (!_mounted) {
        strcpy(_card, "NONE");
        return false;
    }
    switch (SD.cardType()) {
        case CARD_MMC:  strcpy(_card, "MMC");  break;
        case CARD_SD:   strcpy(_card, "SD");   break;
        case CARD_SDHC: strcpy(_card, "SDHC"); break;
        default:        strcpy(_card, "CARD"); break;
    }
    return true;
}

bool Library::scan(const char* path)
{
    if (!_mounted) return false;
    if (!path || !path[0]) path = "/";

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }

    _n = 0;
    File f = dir.openNextFile();
    while (f && _n < MAX_FILES) {
        const char* raw = f.name();
        const char* nm  = baseName(raw ? raw : "");
        if (nm[0] && nm[0] != '.' && strcasecmp(nm, "System Volume Information") != 0) {
            bool isDir = f.isDirectory();
            if (!isDir && isMidiName(nm)) {
                FileEntry& e = _e[_n];
                memset(&e, 0, sizeof(e));
                strncpy(e.name, nm, sizeof(e.name) - 1);
                if (strcmp(path, "/") == 0)
                    snprintf(e.path, sizeof(e.path), "/%s", nm);
                else
                    snprintf(e.path, sizeof(e.path), "%s/%s", path, nm);
                e.dir  = false;
                e.size = (uint32_t)f.size();
                ++_n;
            }
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();

    qsort(_e, _n, sizeof(FileEntry), entryCmp);
    strncpy(_path, path, sizeof(_path) - 1);
    _path[sizeof(_path) - 1] = 0;
    return true;
}

bool Library::enter(int index)
{
    if (index < 0 || index >= _n || !_e[index].dir) return false;
    char next[96];
    strncpy(next, _e[index].path, sizeof(next) - 1);
    next[sizeof(next) - 1] = 0;

    char old[96];
    strncpy(old, _path, sizeof(old) - 1);
    old[sizeof(old) - 1] = 0;

    if (!scan(next)) {
        scan(old);
        return false;
    }
    return true;
}

bool Library::up()
{
    if (strcmp(_path, "/") == 0) return false;
    char parent[96];
    strncpy(parent, _path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = 0;

    char* slash = strrchr(parent, '/');
    if (!slash || slash == parent) strcpy(parent, "/");
    else *slash = 0;

    return scan(parent);
}
