#include "loop_store.h"

#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

LoopStore loopStore;

int LoopStore::vlqLen(uint32_t v)
{
    if (v < 0x80) return 1;
    if (v < 0x4000) return 2;
    if (v < 0x200000) return 3;
    return 4;
}

void LoopStore::writeVlq(uint8_t*& p, uint32_t v)
{
    uint8_t tmp[4];
    int n = 0;
    tmp[n++] = (uint8_t)(v & 0x7F);
    while ((v >>= 7) != 0) tmp[n++] = (uint8_t)((v & 0x7F) | 0x80);
    while (n > 0) *p++ = tmp[--n];
}

bool LoopStore::save(const LoopEvent* ev, int n, uint16_t bpmX10, uint16_t bars,
                     char* outPath, size_t outLen)
{
    if (!ev || n <= 0) return false;
    if (bpmX10 < 400) bpmX10 = 400;
    if (bpmX10 > 4000) bpmX10 = 4000;

    SD.mkdir(LOOP_DIR);

    char path[64] = {0};
    int take = 0;
    for (int i = 1; i <= 9999; ++i) {
        snprintf(path, sizeof(path), LOOP_DIR "/LOOP_%04d.MID", i);
        if (!SD.exists(path)) { take = i; break; }
    }
    if (!take) return false;

    const uint32_t loopLen = (uint32_t)bars * LOOP_BEATS * LOOP_PPQN;
    const uint32_t tempoUs = 60000000u / bpmX10;
    const char* name = "MIDI LOOPER";
    const uint8_t nameLen = (uint8_t)strlen(name);

    uint32_t trackLen = 8 + 7 + (4 + nameLen);  // time sig + tempo + track name
    uint32_t prev = 0;
    for (int i = 0; i < n; ++i) {
        if (ev[i].type < LE_NOTE_ON || ev[i].type > LE_PB) continue;
        uint32_t t = ev[i].tick;
        if (t > loopLen) t = loopLen;
        if (t < prev) t = prev;
        trackLen += (uint32_t)vlqLen(t - prev) + 3;
        prev = t;
    }
    trackLen += (uint32_t)vlqLen(loopLen - prev) + 3;

    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;

    uint8_t hdr[22] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1,
        (uint8_t)(LOOP_PPQN >> 8), (uint8_t)LOOP_PPQN,
        'M','T','r','k',
        (uint8_t)(trackLen >> 24), (uint8_t)(trackLen >> 16),
        (uint8_t)(trackLen >> 8),  (uint8_t)trackLen
    };
    f.write(hdr, sizeof(hdr));

    const uint8_t sig[8] = {0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08};
    f.write(sig, sizeof(sig));

    uint8_t tempo[7] = {0x00, 0xFF, 0x51, 0x03,
                        (uint8_t)(tempoUs >> 16), (uint8_t)(tempoUs >> 8), (uint8_t)tempoUs};
    f.write(tempo, sizeof(tempo));

    uint8_t nameHdr[4] = {0x00, 0xFF, 0x03, nameLen};
    f.write(nameHdr, sizeof(nameHdr));
    f.write((const uint8_t*)name, nameLen);

    prev = 0;
    for (int i = 0; i < n; ++i) {
        if (ev[i].type < LE_NOTE_ON || ev[i].type > LE_PB) continue;
        if ((i & 255) == 0) delay(1);   // keep the idle task fed on big loops
        uint32_t t = ev[i].tick;
        if (t > loopLen) t = loopLen;
        if (t < prev) t = prev;
        uint8_t buf[8];
        uint8_t* p = buf;
        writeVlq(p, t - prev);
        prev = t;

        const LoopEvent& e = ev[i];
        switch (e.type) {
            case LE_NOTE_ON:
                *p++ = (uint8_t)(0x90 | (e.ch & 15)); *p++ = e.d1 & 0x7F; *p++ = e.d2 & 0x7F;
                break;
            case LE_NOTE_OFF:
                *p++ = (uint8_t)(0x80 | (e.ch & 15)); *p++ = e.d1 & 0x7F; *p++ = 0;
                break;
            case LE_CC64:
                *p++ = (uint8_t)(0xB0 | (e.ch & 15)); *p++ = 64; *p++ = e.d2 & 0x7F;
                break;
            case LE_CC1:
                *p++ = (uint8_t)(0xB0 | (e.ch & 15)); *p++ = 1; *p++ = e.d2 & 0x7F;
                break;
            case LE_PB:
                *p++ = (uint8_t)(0xE0 | (e.ch & 15)); *p++ = e.d1 & 0x7F; *p++ = e.d2 & 0x7F;
                break;
            default:
                break;
        }
        f.write(buf, (size_t)(p - buf));
    }

    uint8_t eot[7];
    uint8_t* p = eot;
    writeVlq(p, loopLen - prev);
    *p++ = 0xFF; *p++ = 0x2F; *p++ = 0x00;
    f.write(eot, (size_t)(p - eot));
    f.close();

    if (outPath && outLen) {
        strncpy(outPath, path, outLen - 1);
        outPath[outLen - 1] = 0;
    }
    return true;
}
