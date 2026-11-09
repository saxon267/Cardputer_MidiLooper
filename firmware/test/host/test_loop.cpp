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
    e.setQuantize(Q_OFF);           // exact phase for this test

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
        if (ev[i].ch == 1 && LoopEngine::typeOf(ev[i]) == LE_NOTE_ON) best = ev[i].tick;
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

static void testEarlyStopKeepsLength()
{
    LoopEngine e;
    resetEngine(e, 4, 1200);

    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 0, 60, 100);
    block(e, 300);                  // well before the end of 4 bars
    g_test_us += 500000;
    noteOff(e, 0, 60);
    e.stopRecord();

    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.loopLenTicks() == 4u * 4u * 480u);  // bars unchanged
    CHECK(e.positionTicks() == 0);              // restarts at 0
    CHECK(e.count() >= 2);
}

static void testOverdubFromStop()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 0, 60, 100);
    g_test_us += 1000000;
    noteOff(e, 0, 60);
    block(e, 100);
    e.stopRecord();
    e.stop();

    const int base = e.count();
    CHECK(e.startOverdub());
    CHECK(e.state() == LoopState::REC);
    CHECK(e.positionTicks() == 0);

    const uint8_t flags = block(e, 1);
    CHECK((flags & 0x03) == 0x03);  // downbeat pulse on restart

    noteOn(e, 1, 67, 90);
    e.stopRecord();
    CHECK(e.count() == base + 1);
    CHECK(e.laneCount(1) == 1);
}

static void testCapacity()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 0, 60, 100);
    for (int i = 1; i < MAX_LOOP_EVENTS; ++i) {
        noteOn(e, 0, (uint8_t)((i % 12) + 60), 100);
    }
    CHECK(e.laneCount(0) == MAX_LOOP_EVENTS);

    noteOn(e, 0, 72, 100);          // one too many
    CHECK(e.overflow());
    e.service();
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.count() == MAX_LOOP_EVENTS);
}

static void testBeatFlags()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    CHECK(e.startFirst());
    g_test_us = 1000;
    noteOn(e, 0, 60, 100);
    block(e, 100);
    g_test_us += 1000000;
    noteOff(e, 0, 60);
    block(e, 100);
    e.stopRecord();

    CHECK(e.play());
    uint8_t f = block(e, 1);
    CHECK((f & 0x03) == 0x03);      // start accent

    int normal = 0, accent = 0;
    for (int i = 0; i < 400; ++i) {
        f = block(e, 1);
        if (f & 0x02) ++accent;
        else if (f & 0x01) ++normal;
    }
    CHECK(normal == 1);             // one beat at 480 ticks, no bar accent yet
    CHECK(accent == 0);
}

static void testQuantize()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);
    e.setQuantize(Q_1_16);
    CHECK(e.quantize() == Q_1_16);
    CHECK(e.gridTicks() == 120);

    CHECK(e.startFirst());
    g_test_us = 0;
    noteOn(e, 0, 60, 100);              // tick 0
    g_test_us = 104200;                 // t = 100 ticks -> snapped 120
    noteOn(e, 0, 62, 100);
    g_test_us = 125000;                 // t = 120 ticks: off must stay after on
    noteOff(e, 0, 62);
    e.stopRecord();
    CHECK(e.count() == 3);
    CHECK(e.events()[1].tick == 120);
    CHECK(e.events()[2].tick == 121);
    CHECK(LoopEngine::typeOf(e.events()[2]) == LE_NOTE_OFF);

    // Q_OFF leaves the raw tick.
    resetEngine(e, 1, 1200);
    e.setQuantize(Q_OFF);
    CHECK(e.startFirst());
    g_test_us = 0;
    noteOn(e, 0, 60, 100);
    g_test_us = 104200;
    noteOn(e, 0, 62, 100);
    e.stopRecord();
    CHECK(e.events()[1].tick == 100);
}

static LoopEngine* g_swingEngine = nullptr;
struct SwingHit { uint32_t ev; uint32_t at; uint8_t ch; };
static SwingHit g_hits[8] = {};
static int g_hitCount = 0;

static void swingSink(const LoopEvent& e)
{
    if (g_hitCount < 8) {
        g_hits[g_hitCount].ev = e.tick;
        g_hits[g_hitCount].at = g_swingEngine->positionTicks();
        g_hits[g_hitCount].ch = e.ch;
        ++g_hitCount;
    }
}

static void testSwing()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);
    e.setQuantize(Q_OFF);
    e.setSwing(66);

    CHECK(e.swingOffsetTicks(240) == 76);   // (66-50)*240/50
    CHECK(e.swingOffsetTicks(480) == 0);
    CHECK(e.swingOffsetTicks(120) == 0);

    CHECK(e.startFirst());
    g_test_us = 0;
    noteOn(e, 0, 60, 100);              // tick 0
    g_test_us = 250000;
    noteOn(e, 0, 62, 100);              // tick 240 (odd 8th)
    g_test_us = 500000;
    noteOn(e, 0, 64, 100);              // tick 480 (downbeat)
    e.stopRecord();
    CHECK(e.count() == 3);

    g_hitCount = 0;
    g_swingEngine = &e;
    e.setSink(swingSink);
    e.play();
    for (int i = 0; i < 400; ++i) block(e, 1);

    CHECK(g_hitCount == 3);
    CHECK(g_hits[0].ev == 0);
    CHECK(g_hits[1].ev == 240);
    CHECK(g_hits[1].at >= 315 && g_hits[1].at <= 320);  // delayed by ~76
    CHECK(g_hits[2].ev == 480);
    CHECK(g_hits[2].at >= 480 && g_hits[2].at <= 482);  // downbeat stays
    e.setSink(nullptr);
}

static void testUndo()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);
    e.setQuantize(Q_OFF);

    // Base: tick 0 and tick 480.
    CHECK(e.startFirst());
    g_test_us = 0;
    noteOn(e, 0, 60, 100);
    g_test_us = 500000;
    noteOn(e, 0, 62, 100);
    e.stopRecord();
    CHECK(e.count() == 2);

    // Overdub two events that would interleave with the base if sorted.
    CHECK(e.startOverdub());
    block(e, 140);
    noteOn(e, 1, 67, 100);              // ~tick 195
    block(e, 70);
    noteOn(e, 1, 69, 100);              // ~tick 292
    e.stopRecord();
    CHECK(e.count() == 4);

    CHECK(e.undo());
    CHECK(e.count() == 2);
    CHECK(e.events()[0].tick == 0);
    CHECK(e.events()[1].tick == 480);   // base survives, not the first 2 sorted
    CHECK(!e.undo());

    // U during an active overdub cancels the take.
    CHECK(e.startOverdub());
    block(e, 50);
    noteOn(e, 2, 72, 100);
    CHECK(e.undo());
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.count() == 2);
}

static void testMute()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);
    e.setQuantize(Q_OFF);

    CHECK(e.startFirst());
    g_test_us = 0;
    noteOn(e, 0, 60, 100);              // CH1 tick 0
    g_test_us = 500000;
    noteOn(e, 1, 64, 100);              // CH2 tick 480
    e.stopRecord();

    CHECK(e.toggleMute(0));             // mute CH1
    CHECK(e.mute(0));

    g_hitCount = 0;
    g_swingEngine = &e;
    e.setSink(swingSink);
    e.play();
    for (int i = 0; i < 400; ++i) block(e, 1);

    CHECK(g_hitCount == 1);
    CHECK(g_hits[0].ch == 1);           // only CH2
    e.setSink(nullptr);
}

static void testLoad()
{
    LoopEngine e;
    resetEngine(e, 1, 1200);

    LoopEvent* buf = e.loadBuffer();
    CHECK(buf != nullptr);
    buf[0] = {0,   LE_NOTE_ON, 60, 100, 0};
    buf[1] = {240, LE_NOTE_ON, 64, 100, 1};
    CHECK(e.loadCommit(2, 4));

    CHECK(e.count() == 2);
    CHECK(e.bars() == 4);
    CHECK(e.state() == LoopState::STOP);
    CHECK(e.events()[0].tick == 0);
    CHECK(e.events()[1].tick == 240);
    CHECK(LoopEngine::typeOf(e.events()[0]) == LE_NOTE_ON);

    CHECK(e.play());
    g_hitCount = 0;
    g_swingEngine = &e;
    e.setSink(swingSink);
    for (int i = 0; i < 400; ++i) block(e, 1);
    CHECK(g_hitCount == 2);
    e.setSink(nullptr);
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
    testEarlyStopKeepsLength();
    testOverdubFromStop();
    testCapacity();
    testBeatFlags();
    testQuantize();
    testSwing();
    testUndo();
    testMute();
    testLoad();
    testClearAndStop();
    testArmTimeout();

    if (g_fail) {
        printf("RESULT FAIL (%d)\n", g_fail);
        return 1;
    }
    printf("RESULT OK\n");
    return 0;
}
