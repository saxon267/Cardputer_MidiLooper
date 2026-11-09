#pragma once
#include <stddef.h>
#include <stdint.h>
#include "loop.h"

class LoopStore {
public:
    // Writes a Format-0 SMF with tempo, 4/4 and a track name. EOT is placed at
    // loopLen so the file length matches the loop, not the last event.
    bool save(const LoopEvent* ev, int n, uint16_t bpmX10, uint16_t bars,
              char* outPath, size_t outLen);

private:
    static int  vlqLen(uint32_t v);
    static void writeVlq(uint8_t*& p, uint32_t v);
};

extern LoopStore loopStore;
