#pragma once
#include <stddef.h>
#include <stdint.h>
#include "loop.h"

class LoopStore {
public:
    struct LoadInfo {
        uint16_t bars = 4;
        uint16_t bpmX10 = 1200;
        uint32_t loopTicks = 0;
    };

    // Writes a Format-0 SMF with tempo, 4/4 and a track name. EOT is placed at
    // loopLen so the file length matches the loop, not the last event.
    bool save(const LoopEvent* ev, int n, uint16_t bpmX10, uint16_t bars,
              char* outPath, size_t outLen);

    // Reads Format 0/1, merges tracks, converts to 480 PPQN. The caller owns
    // `out` (normally LoopEngine::loadBuffer()).
    bool load(const char* path, LoopEvent* out, int max, int& n, LoadInfo& info);

private:
    static int  vlqLen(uint32_t v);
    static void writeVlq(uint8_t*& p, uint32_t v);
};

extern LoopStore loopStore;
