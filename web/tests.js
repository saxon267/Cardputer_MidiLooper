/* Browser/Node test suite for the LoopEngine model (web/engine.js).
 * Mirrors firmware/test/host/test_loop.cpp scenario-for-scenario. */
(function (root, factory) {
  const api = factory(
    typeof module === 'object' && module.exports
      ? require('./engine.js')
      : root
  );
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.LoopTests = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function (mod) {
  const { LoopEngine, LoopState, LoopEventType } = mod;

  function runTests() {
    const results = [];

    function test(name, fn) {
      try {
        fn();
        results.push({ name, pass: true, error: '' });
      } catch (e) {
        results.push({ name, pass: false, error: e && e.message ? e.message : String(e) });
      }
    }

    function assert(cond, msg) {
      if (!cond) throw new Error(msg || 'assertion failed');
    }
    function eq(actual, expected, msg) {
      if (actual !== expected) {
        throw new Error((msg || 'value') + ': expected ' + expected + ', got ' + actual);
      }
    }

    function block(e, n = 1) {
      let flags = 0;
      for (let i = 0; i < n; ++i) {
        e.nowMs += 1;
        flags = e.pump(64);
      }
      return flags;
    }

    function fresh(bars = 1, bpmX10 = 1200) {
      const e = new LoopEngine();
      e.setBars(bars);
      e.setBpm(bpmX10);
      return e;
    }

    function makeLoop(e) {
      // Four events on CH1: on 60, on 62, off 60, off 62.
      e.startFirst();
      e.onMidi(0x90, 60, 100, 1000);
      e.onMidi(0x90, 62, 100, 501000);
      e.onMidi(0x80, 60, 0, 1001000);
      e.onMidi(0x80, 62, 0, 1001000);
      block(e, 100);
      e.stopRecord();
    }

    test('defaults and clamps (bars/BPM/loop length)', () => {
      const e = fresh(0, 100);
      eq(e.bars, 1, 'bars clamped');
      eq(e.bpmX10, 400, 'bpm clamped low');
      eq(e.loopLenTicks(), 1920, '1 bar at 480 PPQN');

      e.setBars(99);
      eq(e.bars, 32, 'bars clamped high');
      eq(e.loopLenTicks(), 61440, '32 bars');

      e.setBpm(9999);
      eq(e.bpmX10, 4000, 'bpm clamped high');
    });

    test('first take: arm → first note tick 0 → stop → play', () => {
      const e = fresh(1, 1200);
      assert(e.startFirst(), 'startFirst');
      eq(e.state, LoopState.ARMED);

      e.onMidi(0x90, 60, 100, 0);
      eq(e.state, LoopState.REC);
      eq(e.count(), 0, 'first take is in the inactive buffer');
      eq(e.laneCount(0), 1, 'live lane count');

      block(e, 100);
      e.onMidi(0x80, 60, 0, 1000000);   // 1 s later = 960 ticks at 120 BPM
      block(e, 100);

      e.stopRecord();
      eq(e.state, LoopState.PLAY);
      assert(e.hasLoop(), 'has loop');
      eq(e.count(), 2);
      eq(e.events[0].tick, 0, 'first note at 0');
      eq(e.events[1].tick, 960, 'note off at 960');

      assert(e.play(), 'play');
      eq(e.positionTicks(), 0);
      const flags = block(e, 1);
      eq(flags & 0x03, 0x03, 'downbeat pulse');

      block(e, 1380);                    // > 1 bar at 120 BPM
      eq(e.state, LoopState.PLAY, 'playback wraps, does not stop');
    });

    test('auto-stop at loop end', () => {
      const e = fresh(1, 1200);
      e.startFirst();
      e.onMidi(0x91, 64, 100, 1000);
      eq(e.state, LoopState.REC);

      block(e, 1400);
      assert(e.autoStop, 'autoStop flag set at wrap');
      e.service(e.nowMs);
      eq(e.state, LoopState.PLAY);
      eq(e.count(), 1);
      eq(e.laneCount(1), 1);
    });

    test('overdub starts at current phase and merges', () => {
      const e = fresh(1, 1200);
      makeLoop(e);
      const base = e.count();
      eq(base, 4);

      assert(e.startOverdub(), 'startOverdub');
      eq(e.state, LoopState.REC);
      assert(e.overdub, 'overdub flag');

      block(e, 200);
      const phase = e.positionTicks();
      e.onMidi(0x91, 67, 90, 2000000);
      e.onMidi(0x81, 67, 0, 2000000);
      e.stopRecord();

      eq(e.state, LoopState.PLAY);
      eq(e.count(), base + 2);
      eq(e.laneCount(1), 1);

      let best = -1;
      for (const ev of e.events) {
        if (ev.ch === 1 && ev.type === LoopEventType.NOTE_ON) best = ev.tick;
      }
      assert(best >= 0, 'overdubbed note exists');
      assert(Math.abs(best - phase) <= 2,
        'overdubbed tick near phase: ' + best + ' vs ' + phase);
    });

    test('overdub when stopped starts at zero', () => {
      const e = fresh(1, 1200);
      makeLoop(e);
      e.stop();
      const base = e.count();

      assert(e.startOverdub(), 'overdub from stop');
      eq(e.phaseTick, 0);
      const flags = block(e, 1);
      eq(flags & 0x03, 0x03, 'downbeat pulse on restart');

      e.onMidi(0x90, 60, 100, 5000);
      e.stopRecord();
      eq(e.count(), base + 1);
    });

    test('channel filter and lane mapping', () => {
      const e = fresh(1, 1200);
      e.startFirst();
      e.onMidi(0x93, 60, 100, 1000);      // CH4 ignored
      eq(e.state, LoopState.ARMED);
      e.onMidi(0x99, 36, 100, 2000);      // CH10 drums accepted
      eq(e.state, LoopState.REC);
      eq(e.laneCount(3), 1);

      eq(mod.laneOfChannel(0), 0);
      eq(mod.laneOfChannel(1), 1);
      eq(mod.laneOfChannel(2), 2);
      eq(mod.laneOfChannel(9), 3);
      eq(mod.laneOfChannel(3), -1);
      assert(mod.channelAllowed(9));
      assert(!mod.channelAllowed(4));
    });

    test('early stop keeps the selected bar length', () => {
      const e = fresh(4, 1200);
      e.startFirst();
      e.onMidi(0x90, 60, 100, 1000);
      block(e, 300);                      // early: well before 4 bars
      e.onMidi(0x80, 60, 0, 500000);
      e.stopRecord();

      eq(e.state, LoopState.PLAY);
      eq(e.loopLenTicks(), 4 * 4 * 480, 'bars unchanged');
      eq(e.positionTicks(), 0, 'playback restarts at 0');
      assert(e.count() >= 1);
    });

    test('capacity: 8192 events then overflow', () => {
      const e = fresh(1, 1200);
      e.startFirst();
      e.onMidi(0x90, 60, 100, 1000);       // 1 event
      for (let i = 1; i < mod.MAX_EVENTS; ++i) {
        e.onMidi(0x90, (i % 12) + 60, 100, 1000);
      }
      eq(e.inactive.length, mod.MAX_EVENTS, 'filled to capacity');
      e.onMidi(0x90, 72, 100, 1000);       // one too many
      assert(e.overflow, 'overflow flag');
      assert(e.autoStop, 'auto stop on overflow');

      e.service(e.nowMs);
      eq(e.state, LoopState.PLAY);
      eq(e.count(), mod.MAX_EVENTS);
    });

    test('stop keeps loop; clear removes it', () => {
      const e = fresh(1, 1200);
      makeLoop(e);
      assert(e.hasLoop());
      e.stop();
      eq(e.state, LoopState.STOP);
      assert(e.hasLoop(), 'stop keeps RAM');
      e.clear();
      assert(!e.hasLoop());
      eq(e.count(), 0);
      eq(e.state, LoopState.STOP);
    });

    test('ARMED times out after 20 s', () => {
      const e = fresh(1, 1200);
      e.nowMs = 0;
      assert(e.startFirst());
      eq(e.state, LoopState.ARMED);
      e.service(20001);
      eq(e.state, LoopState.STOP);
    });

    test('beat flags: downbeat accent then normal beats', () => {
      const e = fresh(1, 1200);
      makeLoop(e);
      e.play();
      const first = block(e, 1);
      eq(first & 0x03, 0x03, 'start accent');

      // 480 ticks per beat at 120 BPM; ~345 blocks per beat.
      let normal = 0, accent = 0;
      for (let i = 0; i < 400; ++i) {
        const f = block(e, 1);
        if (f & 0x02) ++accent;
        else if (f & 0x01) ++normal;
      }
      eq(normal, 1, 'one normal beat in the window');
      eq(accent, 0, 'bar accent is not due yet');
    });

    return results;
  }

  return { runTests };
});
