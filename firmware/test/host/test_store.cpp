// Host-side test for the SMF writer (loop_store.cpp) with a fake SD card.
//   g++ -std=c++17 -I test/host -I src test/host/test_store.cpp src/loop_store.cpp -o test_store
#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "loop_store.h"

unsigned long g_test_ms = 0;
unsigned long g_test_us = 0;
std::vector<uint8_t> g_sd_buf;
SDClass SD;

static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            ++g_fail;                                                      \
        }                                                                  \
    } while (0)

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int main()
{
    const LoopEvent ev[2] = {
        {0,   LE_NOTE_ON,  60, 100, 0},
        {960, LE_NOTE_OFF, 60, 0,   0},
    };

    char path[64] = {0};
    CHECK(loopStore.save(ev, 2, 1200, 4, path, sizeof(path)));
    CHECK(strcmp(path, "/MidiLoop/LOOP_0001.MID") == 0);

    // Header: MThd, length 6, format 0, one track, 480 PPQN.
    CHECK(g_sd_buf.size() == 66);
    CHECK(memcmp(&g_sd_buf[0], "MThd", 4) == 0);
    CHECK(be32(&g_sd_buf[4]) == 6);
    CHECK(g_sd_buf[8] == 0 && g_sd_buf[9] == 0);          // format 0
    CHECK(g_sd_buf[10] == 0 && g_sd_buf[11] == 1);        // one track
    CHECK(g_sd_buf[12] == 0x01 && g_sd_buf[13] == 0xE0); // division 480

    CHECK(memcmp(&g_sd_buf[14], "MTrk", 4) == 0);
    const uint32_t trackLen = be32(&g_sd_buf[18]);
    CHECK(trackLen == g_sd_buf.size() - 22);              // length field is exact

    // Time signature 4/4 at 22.
    CHECK(g_sd_buf[22] == 0x00 && g_sd_buf[23] == 0xFF &&
          g_sd_buf[24] == 0x58 && g_sd_buf[25] == 0x04);
    CHECK(g_sd_buf[26] == 0x04 && g_sd_buf[27] == 0x02 &&
          g_sd_buf[28] == 0x18 && g_sd_buf[29] == 0x08);

    // Tempo at 30: 60e6 / 1200 = 50000 us = 0x00C350.
    CHECK(g_sd_buf[30] == 0x00 && g_sd_buf[31] == 0xFF &&
          g_sd_buf[32] == 0x51 && g_sd_buf[33] == 0x03);
    const uint32_t tempo = ((uint32_t)g_sd_buf[34] << 16) |
                           ((uint32_t)g_sd_buf[35] << 8) | g_sd_buf[36];
    CHECK(tempo == 50000);                                // 120 BPM

    CHECK(g_sd_buf[37] == 0x00 && g_sd_buf[38] == 0xFF &&
          g_sd_buf[39] == 0x03 && g_sd_buf[40] == 11);
    CHECK(memcmp(&g_sd_buf[41], "MIDI LOOPER", 11) == 0);

    // Event 1 at 52: delta 0 + note-on.
    CHECK(g_sd_buf[52] == 0x00 && g_sd_buf[53] == 0x90 &&
          g_sd_buf[54] == 60 && g_sd_buf[55] == 100);

    // Event 2 at 56: delta 960 (0x87 0x40) + note-off.
    CHECK(g_sd_buf[56] == 0x87 && g_sd_buf[57] == 0x40 &&
          g_sd_buf[58] == 0x80 && g_sd_buf[59] == 60 && g_sd_buf[60] == 0);

    // EOT at 61: delta 6720 (0xB4 0x40) + FF 2F 00.
    CHECK(g_sd_buf[61] == 0xB4 && g_sd_buf[62] == 0x40 &&
          g_sd_buf[63] == 0xFF && g_sd_buf[64] == 0x2F && g_sd_buf[65] == 0x00);

    if (g_fail) {
        printf("RESULT FAIL (%d)\n", g_fail);
        return 1;
    }
    printf("RESULT OK\n");
    return 0;
}
