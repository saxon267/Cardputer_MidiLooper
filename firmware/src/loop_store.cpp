#include "loop_store.h"

#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LoopStore loopStore;

static uint16_t be16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static bool rdBytes(File& f, int& pos, int len, uint8_t* dst, int n)
{
    if (n < 0 || pos + n > len) return false;
    if ((int)f.read(dst, (size_t)n) != n) return false;
    pos += n;
    return true;
}

static int rdByte(File& f, int& pos, int len)
{
    uint8_t b;
    if (!rdBytes(f, pos, len, &b, 1)) return -1;
    return b;
}

static bool rdVlq(File& f, int& pos, int len, uint32_t& out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const int b = rdByte(f, pos, len);
        if (b < 0) return false;
        v = (v << 7) | (uint32_t)(b & 0x7F);
        if ((b & 0x80) == 0) {
            out = v;
            return true;
        }
    }
    return false;
}

static int eventCmp(const void* a, const void* b)
{
    const LoopEvent* x = static_cast<const LoopEvent*>(a);
    const LoopEvent* y = static_cast<const LoopEvent*>(b);
    if (x->tick < y->tick) return -1;
    if (x->tick > y->tick) return 1;
    return 0;
}

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

bool LoopStore::load(const char* path, LoopEvent* out, int max, int& n, LoadInfo& info)
{
    n = 0;
    if (!path || !out || max <= 0) return false;

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    auto fail = [&]() -> bool {
        f.close();
        n = 0;
        return false;
    };

    uint8_t hdr[14];
    if (f.read(hdr, sizeof(hdr)) != (int)sizeof(hdr)) return fail();
    if (memcmp(hdr, "MThd", 4) != 0) return fail();

    const uint32_t hdrLen = be32(hdr + 4);
    if (hdrLen < 6) return fail();
    const uint16_t format = be16(hdr + 8);
    const uint16_t ntracks = be16(hdr + 10);
    const uint16_t division = be16(hdr + 12);
    if (format == 2 || division == 0 || (division & 0x8000)) return fail();

    for (uint32_t i = 6; i < hdrLen; ++i) {
        uint8_t dummy;
        if (f.read(&dummy, 1) != 1) return fail();
    }

    uint32_t tempoUs = 0;
    uint32_t maxTick = 0;

    for (uint16_t t = 0; t < ntracks; ++t) {
        uint8_t th[8];
        if (f.read(th, sizeof(th)) != (int)sizeof(th)) return fail();
        if (memcmp(th, "MTrk", 4) != 0) return fail();

        const uint32_t tlen = be32(th + 4);
        int pos = 0;
        uint32_t abs = 0;
        uint8_t status = 0;

        while (pos < (int)tlen) {
            uint32_t delta = 0;
            if (!rdVlq(f, pos, (int)tlen, delta)) return fail();
            abs += delta;

            const int b = rdByte(f, pos, (int)tlen);
            if (b < 0) return fail();

            int data1 = -1;
            if (b & 0x80) {
                status = (uint8_t)b;
            } else {
                if (status < 0x80) return fail();
                data1 = b;
            }

            if (status == 0xFF) {
                const int meta = rdByte(f, pos, (int)tlen);
                uint32_t mlen = 0;
                if (meta < 0 || !rdVlq(f, pos, (int)tlen, mlen)) return fail();
                if (meta == 0x51 && mlen == 3) {
                    uint8_t t3[3];
                    if (!rdBytes(f, pos, (int)tlen, t3, 3)) return fail();
                    if (tempoUs == 0) {
                        tempoUs = ((uint32_t)t3[0] << 16) | ((uint32_t)t3[1] << 8) | t3[2];
                    }
                } else if (mlen > 0) {
                    for (uint32_t i = 0; i < mlen; ++i) {
                        if (rdByte(f, pos, (int)tlen) < 0) return fail();
                    }
                }
                status = 0;
                continue;
            }

            if (status == 0xF0 || status == 0xF7) {
                uint32_t slen = 0;
                if (!rdVlq(f, pos, (int)tlen, slen)) return fail();
                for (uint32_t i = 0; i < slen; ++i) {
                    if (rdByte(f, pos, (int)tlen) < 0) return fail();
                }
                status = 0;
                continue;
            }

            if (status < 0x80) return fail();

            const uint8_t hi = status & 0xF0;
            const uint8_t ch = status & 0x0F;
            const int need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            uint8_t d[2] = {0, 0};

            if (data1 >= 0) {
                d[0] = (uint8_t)data1;
            } else {
                const int v = rdByte(f, pos, (int)tlen);
                if (v < 0) return fail();
                d[0] = (uint8_t)v;
            }
            if (need == 2) {
                const int v = rdByte(f, pos, (int)tlen);
                if (v < 0) return fail();
                d[1] = (uint8_t)v;
            }

            uint8_t type = 0;
            uint8_t e1 = 0, e2 = 0;
            if (hi == 0x90) {
                if (d[1] == 0) { type = LE_NOTE_OFF; e1 = d[0]; }
                else           { type = LE_NOTE_ON;  e1 = d[0]; e2 = d[1]; }
            } else if (hi == 0x80) {
                type = LE_NOTE_OFF; e1 = d[0];
            } else if (hi == 0xB0 && d[0] == 64) {
                type = LE_CC64; e1 = 64; e2 = d[1];
            } else if (hi == 0xB0 && d[0] == 1) {
                type = LE_CC1; e1 = 1; e2 = d[1];
            } else if (hi == 0xE0) {
                type = LE_PB; e1 = d[0]; e2 = d[1];
            }

            if (type) {
                if (n >= max) return fail();
                const uint64_t tick = (uint64_t)abs * LOOP_PPQN / division;
                out[n].tick = (uint32_t)tick;
                out[n].type = type;          // generation 0 for file data
                out[n].d1 = e1;
                out[n].d2 = e2;
                out[n].ch = ch;
                ++n;
            }
        }

        if (abs > maxTick) maxTick = abs;
    }
    f.close();

    if (n <= 0 || maxTick == 0) return false;

    qsort(out, (size_t)n, sizeof(LoopEvent), eventCmp);

    const uint64_t ticks480 = (uint64_t)maxTick * LOOP_PPQN / division;
    uint32_t bars = (uint32_t)((ticks480 + (LOOP_BEATS * LOOP_PPQN) - 1) / (LOOP_BEATS * LOOP_PPQN));
    if (bars < 1) bars = 1;
    if (bars > LOOP_MAX_BARS) bars = LOOP_MAX_BARS;
    const uint32_t loopTicks = bars * LOOP_BEATS * LOOP_PPQN;

    int w = 0;
    for (int i = 0; i < n; ++i) {
        if (out[i].tick < loopTicks) out[w++] = out[i];
    }
    n = w;
    if (n <= 0) return false;

    info.bars = (uint16_t)bars;
    info.loopTicks = loopTicks;
    if (tempoUs > 0) {
        // v0.1 builds wrote tempo/10 (e.g. 50000 for 120 BPM). Valid app tempos
        // are 150000..1500000 us, so a smaller value is treated as legacy.
        if (tempoUs < 150000u) tempoUs *= 10u;
        uint32_t b = 600000000u / tempoUs;
        if (b < 400) b = 400;
        if (b > 4000) b = 4000;
        info.bpmX10 = (uint16_t)b;
    } else {
        info.bpmX10 = 1200;
    }
    return true;
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
    const uint32_t tempoUs = 600000000u / bpmX10;
    const char* name = "MIDI LOOPER";
    const uint8_t nameLen = (uint8_t)strlen(name);

    uint32_t trackLen = 8 + 7 + (4 + nameLen);  // time sig + tempo + track name
    uint32_t prev = 0;
    for (int i = 0; i < n; ++i) {
        const uint8_t type = LoopEngine::typeOf(ev[i]);
        if (type < LE_NOTE_ON || type > LE_PB) continue;
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
        const uint8_t type = LoopEngine::typeOf(ev[i]);
        if (type < LE_NOTE_ON || type > LE_PB) continue;
        if ((i & 255) == 0) delay(1);   // keep the idle task fed on big loops
        uint32_t t = ev[i].tick;
        if (t > loopLen) t = loopLen;
        if (t < prev) t = prev;
        uint8_t buf[8];
        uint8_t* p = buf;
        writeVlq(p, t - prev);
        prev = t;

        const LoopEvent& e = ev[i];
        switch (LoopEngine::typeOf(e)) {
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
