// Host-side test for the pure LoopEngine (no hardware, no Arduino runtime).
// Build (needs a host C++17 compiler, e.g. g++ or `zig c++`):
//   g++ -std=c++17 -I test/host -I src test/host/test_loop.cpp src/loop.cpp -o test_loop
//   ./test_loop
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "loop.h"

unsigned long g_test_ms = 0;
unsigned long g_test_us = 0;

static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            ++g_fail;                                                      \
        }                                                                  \
    } while (0)

static void noteOn(LoopEngine& e, uint8_t ch, uint8_t note, uint8_t vel = 100)
{
    e.onMidi((uint8_t)(0x90 | ch), note, vel, (uint32_t)g_test_us);
}

static void noteOff(LoopEngine& e, uint8_t ch, uint8_t note)
{
    e.onMidi((uint8_t)(0x80 | ch), note, 0, (uint32_t)g_test_us);
}

// One audio block at 44.1 kHz / 64 frames; mirrors AudioEngine's pump call.
static uint8_t block(LoopEngine& e, int n = 1)
{
    uint8_t flags = 0;
    for (int i = 0; i < n; ++i) {
        g_test_ms += 1;          // not used by pump, but keeps arm-time sane
        flags = e.pump(64);
    }
    return flags;
}

static void resetEngine(LoopEngine& e, uint16_t bars = 1, uint16_t bpmX10 = 1200)
{
    e.begin();
    e.setBars(bars);
    e.setBpm(bpmX10);
    g_test_ms = 0;
    g_test_us = 0;
}

static void testDefaultsAndClamps()
{
    LoopEngine e;
    resetEngine(e, 0, 100);
    CHECK(e.bars() == 1);
    CHECK(e.bpmX10() == 400);
    CHECK(e.loopLenTicks() == 1920);

    e.setBars(99);
    CHECK(e.bars() == 32);
    CHECK(e.loopLenTicks() == 61440);

    e.setBpm(9999);
    CHECK(e.bpmX10() == 4000);
}

static void testFirstTakeAndPlayback()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    CHECK(e.state() == LoopState::ARMED);

    // First MIDI defines tick 0.
    noteOn(e, 0, 60, 100);
    CHECK(e.state() == LoopState::REC);
    CHECK(e.count() == 0);          // still in the first-take buffer
    CHECK(e.laneCount(0) == 1);     // live lane count reads the take

    block(e, 100);
    g_test_us += 1000000;           // 1 s later -> a real note-off tick
    noteOff(e, 0, 60);
    block(e, 100);

    e.stopRecord();
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.hasLoop());
    CHECK(e.count() == 2);          // note-on + note-off
    CHECK(e.events()[0].tick == 0);
    CHECK(e.events()[1].tick > 0);

    // play() restarts at 0 and emits a downbeat pulse.
    CHECK(e.play());
    CHECK(e.positionTicks() == 0);
    uint8_t flags = block(e, 1);
    CHECK((flags & 0x03) == 0x03);

    // One bar at 120 BPM / 480 PPQN = 1920 ticks; ~1378 blocks.
    block(e, 1380);
    CHECK(e.state() == LoopState::PLAY);
}

static void testAutoStopAtLoopEnd()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 1, 64, 100);
    CHECK(e.state() == LoopState::REC);

    // Pump past the 1920-tick loop; service() converts auto-stop into PLAY.
    block(e, 1400);
    e.service();
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.count() == 1);
    CHECK(e.laneCount(1) == 1);
}

static void testOverdubMergesAtCurrentPhase()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    // Take 1: two notes on CH1.
    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 0, 60, 100);
    g_test_us += 500000;
    block(e, 300);
    noteOn(e, 0, 62, 100);
    g_test_us += 500000;
    block(e, 300);
    noteOff(e, 0, 60);
    noteOff(e, 0, 62);
    e.stopRecord();
    CHECK(e.state() == LoopState::PLAY);
    const int base = e.count();

    // Overdub starts immediately, at the current phase, not at the next bar.
    CHECK(e.startOverdub());
    CHECK(e.state() == LoopState::REC);
    CHECK(e.isOverdub());

    block(e, 200);                  // advance into the loop
    const uint32_t phase = e.positionTicks();
    noteOn(e, 1, 67, 90);
    noteOff(e, 1, 67);
    e.stopRecord();
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.count() == base + 2);
    CHECK(e.laneCount(1) == 1);

    // The overdubbed event must land near the phase where it was played,
    // not at tick 0.
    const LoopEvent* ev = e.events();
    uint32_t best = 0xFFFFFFFFu;
    for (int i = 0; i < e.count(); ++i) {
        if (ev[i].ch == 1 && ev[i].type == LE_NOTE_ON) best = ev[i].tick;
    }
    CHECK(best != 0xFFFFFFFFu);
    CHECK(best >= phase - 2 && best <= phase + 2);
}

static void testChannelFilterAndLaneMapping()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 3, 60, 100);          // CH4 is not one of 1/2/3/10 -> ignored
    CHECK(e.state() == LoopState::ARMED);
    noteOn(e, 9, 36, 100);          // CH10 drums
    CHECK(e.state() == LoopState::REC);
    CHECK(e.laneCount(3) == 1);
    e.stopRecord();

    CHECK(LoopEngine::laneOfChannel(0) == 0);
    CHECK(LoopEngine::laneOfChannel(1) == 1);
    CHECK(LoopEngine::laneOfChannel(2) == 2);
    CHECK(LoopEngine::laneOfChannel(9) == 3);
    CHECK(LoopEngine::laneOfChannel(3) == -1);
    CHECK(LoopEngine::channelAllowed(9));
    CHECK(!LoopEngine::channelAllowed(4));
}

static void testClearAndStop()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 0, 60, 100);
    e.stopRecord();
    CHECK(e.hasLoop());

    e.stop();
    CHECK(e.state() == LoopState::STOP);
    CHECK(e.hasLoop());             // stop keeps the loop in RAM

    e.clear();
    CHECK(!e.hasLoop());
    CHECK(e.count() == 0);
    CHECK(e.state() == LoopState::STOP);
}

static void testArmTimeout()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    CHECK(e.state() == LoopState::ARMED);
    g_test_ms = 21000;              // service() uses a 20 s arm timeout
    e.service();
    CHECK(e.state() == LoopState::STOP);
}

int main()
{
    testDefaultsAndClamps();
    testFirstTakeAndPlayback();
    testAutoStopAtLoopEnd();
    testOverdubMergesAtCurrentPhase();
    testChannelFilterAndLaneMapping();
    testClearAndStop();
    testArmTimeout();

    if (g_fail) {
        printf("RESULT FAIL (%d)\n", g_fail);
        return 1;
    }
    printf("RESULT OK\n");
    return 0;
}
