/* MIDI Looper — browser-side model of firmware/src/loop.cpp.
 * It mirrors the tick math, state machine and capacity limits so the web UI
 * and the browser test page can exercise the same contract as the firmware.
 * This is still a model, not the C++ binary: USB/audio/SD stay hardware-only. */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else Object.assign(root, api);
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const MAX_EVENTS = 8192;
  const MAX_TAKE   = 4096;
  const PPQN       = 480;
  const BEATS      = 4;
  const AUDIO_RATE = 44100;

  const State = { STOP: 'STOP', ARMED: 'ARMED', REC: 'REC', PLAY: 'PLAY' };
  const Type  = { NOTE_ON: 1, NOTE_OFF: 2, CC64: 3, CC1: 4, PB: 5 };
  const Quant = { OFF: 0, Q4: 1, Q8: 2, Q16: 3, Q32: 4 };
  const typeOf = e => e.type & 0x0F;
  const genOf  = e => e.type >> 4;

  const laneOfChannel = ch => (ch === 0 ? 0 : ch === 1 ? 1 : ch === 2 ? 2 : ch === 9 ? 3 : -1);
  const channelAllowed = ch => laneOfChannel(ch) >= 0;
  const cmpEvent = (a, b) => a.tick - b.tick;

  class LoopEngine {
    constructor() { this.begin(); }

    begin() {
      this.state = State.STOP;
      this.bars = 4;
      this.bpmX10 = 1200;
      this.active = [];
      this.inactive = [];
      this.take = [];
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
      this.startPulse = false;
      this.autoStop = false;
      this.overdub = false;
      this.armed = false;
      this.takeFull = false;
      this.overflow = false;
      this.originUs = 0;
      this.armMs = 0;
      this.nowMs = 0;
      this.lastFlags = 0;
      this.onEvent = null;
      this.quantize = Quant.Q16;
      this.swing = 50;
      this.gen = 1;
      this.undoStack = [];      // { gen, count }
      this.muteLanes = [false, false, false, false];
    }

    loopLenTicks() { return this.bars * BEATS * PPQN; }
    get events() { return this.active; }
    hasLoop() { return this.active.length > 0; }
    count() { return this.active.length; }
    takeCount() { return this.take.length; }
    positionTicks() { return this.phaseTick; }
    phasePermille() {
      const len = this.loopLenTicks();
      return len ? Math.floor(this.phaseTick * 1000 / len) : 0;
    }

    setBars(bars) {
      this.bars = Math.max(1, Math.min(32, bars | 0));
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
    }

    setBpm(bpmX10) {
      this.bpmX10 = Math.max(400, Math.min(4000, bpmX10 | 0));
    }

    setQuantize(q) {
      this.quantize = (q >= 0 && q <= Quant.Q32) ? (q | 0) : Quant.Q16;
    }

    gridTicks() {
      switch (this.quantize) {
        case Quant.Q4:  return PPQN;
        case Quant.Q8:  return PPQN / 2;
        case Quant.Q16: return PPQN / 4;
        case Quant.Q32: return PPQN / 8;
        default:        return 0;
      }
    }

    setSwing(pct) {
      this.swing = Math.max(50, Math.min(75, pct | 0));
    }

    swingOffsetTicks(tick) {
      if (this.swing <= 50) return 0;
      const eighth = PPQN / 2;
      if (tick % eighth !== 0) return 0;         // exact 8th-note positions
      if (((tick / eighth) & 1) === 0) return 0; // downbeat stays straight
      return Math.floor((this.swing - 50) * eighth / 50);
    }

    snapTick(tick) {
      const g = this.gridTicks();
      if (!g) return tick;
      let snapped = Math.floor((tick + g / 2) / g) * g;
      const len = this.loopLenTicks();
      if (snapped >= len) snapped = len > g ? len - g : 0;
      return snapped;
    }

    pushUndo(gen, count) {
      this.undoStack.push({ gen, count });
      if (this.undoStack.length > 8) this.undoStack.shift();
    }

    setMute(lane, on) {
      if (lane >= 0 && lane < 4) this.muteLanes[lane] = !!on;
    }

    mute(lane) {
      return lane >= 0 && lane < 4 && this.muteLanes[lane];
    }

    toggleMute(lane) {
      if (lane < 0 || lane >= 4) return false;
      this.muteLanes[lane] = !this.muteLanes[lane];
      return this.muteLanes[lane];
    }

    load(events, bars) {
      if (!events || !events.length) return false;
      const b = Math.max(1, Math.min(32, bars | 0));
      this.bars = b;
      this.active = events.map(e => ({
        tick: Math.min(e.tick, b * BEATS * PPQN - 1),
        type: e.type & 0x0F,
        d1: e.d1,
        d2: e.d2,
        ch: e.ch,
      })).sort(cmpEvent);
      this.inactive = [];
      this.take = [];
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
      this.state = State.STOP;
      this.overdub = false;
      this.armed = false;
      this.autoStop = false;
      this.startPulse = false;
      this.gen = 1;
      this.undoStack = [];
      return true;
    }

    undo() {
      if (this.state === State.REC && this.overdub) {
        this.take = [];
        this.state = State.PLAY;
        this.overdub = false;
        this.autoStop = false;
        if (this.undoStack.length) this.undoStack.pop();
        return true;
      }
      if (!this.undoStack.length) return false;

      const u = this.undoStack.pop();
      const prev = this.state;
      this.active = this.active.filter(e => genOf(e) !== u.gen);
      this.idx = this.lowerBound(this.active, this.phaseTick);
      this.state = (this.active.length > 0 && prev === State.PLAY) ? State.PLAY : State.STOP;
      this.overdub = false;
      this.take = [];
      this.autoStop = false;
      return true;
    }

    startFirst() {
      if (this.state === State.REC || this.state === State.ARMED) return false;
      this.state = State.ARMED;
      this.overdub = false;
      this.armed = true;
      this.inactive = [];
      this.take = [];
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
      this.autoStop = false;
      this.takeFull = false;
      this.overflow = false;
      this.startPulse = false;
      this.armMs = this.nowMs;
      this.gen = 1;
      this.undoStack = [];
      return true;
    }

    startOverdub() {
      if (!this.hasLoop()) return this.startFirst();
      if (this.state === State.REC || this.state === State.ARMED) return false;

      if (this.gen >= 15) {
        for (const e of this.active) e.type &= 0x0F;
        this.gen = 1;
        this.undoStack = [];
      } else {
        ++this.gen;
      }
      this.pushUndo(this.gen, this.active.length);

      if (this.state !== State.PLAY) {
        this.phaseQ16 = 0;
        this.phaseTick = 0;
        this.idx = 0;
      }
      this.state = State.REC;
      this.overdub = true;
      this.armed = false;
      this.take = [];
      this.autoStop = false;
      this.takeFull = false;
      this.overflow = false;
      this.startPulse = this.phaseTick === 0;
      return true;
    }

    stopRecord() {
      if (this.state === State.ARMED) {
        this.state = State.STOP;
        this.armed = false;
        this.phaseQ16 = 0;
        this.phaseTick = 0;
        this.idx = 0;
        return;
      }
      if (this.state !== State.REC) return;
      const over = this.overdub;
      this.state = State.STOP;
      if (over) this.finishOverdub();
      else this.finishFirst();
    }

    play() {
      if (!this.hasLoop()) return false;
      this.state = State.PLAY;
      this.overdub = false;
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
      this.startPulse = true;
      return true;
    }

    stop() {
      this.state = State.STOP;
      this.overdub = false;
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
    }

    clear() {
      this.state = State.STOP;
      this.overdub = false;
      this.armed = false;
      this.active = [];
      this.inactive = [];
      this.take = [];
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
      this.autoStop = false;
      this.takeFull = false;
      this.overflow = false;
      this.startPulse = false;
    }

    service(nowMs = this.nowMs) {
      if (this.state === State.ARMED) {
        if (nowMs - this.armMs > 20000) {
          this.state = State.STOP;
          this.armed = false;
        }
        return;
      }
      if (this.state === State.REC && this.autoStop) {
        const over = this.overdub;
        this.state = State.STOP;
        if (over) this.finishOverdub();
        else this.finishFirst();
      }
    }

    onMidi(status, d1, d2, nowUs) {
      const hi = status & 0xF0;
      const ch = status & 0x0F;
      if (!channelAllowed(ch)) return;

      const e = { tick: 0, type: 0, d1: 0, d2: 0, ch };
      let type = 0;
      if (hi === 0x90) {
        if (d2 === 0) { type = Type.NOTE_OFF; e.d1 = d1; }
        else          { type = Type.NOTE_ON;  e.d1 = d1; e.d2 = d2; }
      } else if (hi === 0x80) {
        type = Type.NOTE_OFF; e.d1 = d1;
      } else if (hi === 0xB0 && d1 === 64) {
        type = Type.CC64; e.d1 = 64; e.d2 = d2;
      } else if (hi === 0xB0 && d1 === 1) {
        type = Type.CC1; e.d1 = 1; e.d2 = d2;
      } else if (hi === 0xE0) {
        type = Type.PB; e.d1 = d1; e.d2 = d2;
      } else {
        return;
      }
      e.type = ((this.gen << 4) | type) & 0xFF;

      if (this.state === State.ARMED) {
        this.state = State.REC;
        this.armed = false;
        this.overdub = false;
        this.originUs = nowUs;
        this.phaseQ16 = 0;
        this.phaseTick = 0;
        this.idx = 0;
        this.inactive = [];
        this.autoStop = false;
        this.takeFull = false;
        this.overflow = false;
        this.startPulse = true;
        e.tick = 0;
        this.inactive.push(e);          // tick 0
      } else if (this.state === State.REC) {
        if (this.overdub) {
          const len = this.loopLenTicks();
          e.tick = this.snapTick(len ? (this.phaseTick % len) : 0);
          this.appendTake(e);
        } else {
          const t = Math.floor((nowUs - this.originUs) * this.bpmX10 * 8 / 10000000);
          if (t >= this.loopLenTicks()) {
            this.autoStop = true;
          } else if (this.inactive.length < MAX_EVENTS) {
            e.tick = this.snapTick(t);
            this.inactive.push(e);
          } else {
            this.overflow = true;
            this.autoStop = true;
          }
        }
      }
    }

    pump(frames) {
      let flags = 0;
      if (this.state !== State.REC && this.state !== State.PLAY) {
        this.lastFlags = 0;
        return 0;
      }
      if (this.startPulse) {
        this.startPulse = false;
        flags |= 0x03;
      }

      const len = this.loopLenTicks();
      if (!len) { this.lastFlags = flags; return flags; }

      const inc = Math.floor(this.bpmX10 * 8 * frames * 65536 / (10 * AUDIO_RATE));
      let q = this.phaseQ16 + inc;
      const lenQ = len * 65536;
      const prevTick = this.phaseTick;

      if (q >= lenQ) {
        // Flush any tail still held back by swing before wrapping.
        while (this.idx < this.active.length) {
          const e = this.active[this.idx];
          const lane = laneOfChannel(e.ch);
          if (lane < 0 || !this.muteLanes[lane]) {
            if (this.onEvent) this.onEvent(e);
          }
          ++this.idx;
        }
        this.idx = 0;
        q %= lenQ;
        if (this.state === State.REC) this.autoStop = true;
      }
      this.phaseQ16 = q;
      const tick = Math.floor(q / 65536);
      this.phaseTick = tick;

      const prevBeat = Math.floor(prevTick / PPQN);
      const beat = Math.floor(tick / PPQN);
      if (beat !== prevBeat) {
        flags |= 0x01;
        if (beat % BEATS === 0) flags |= 0x02;
      }

      while (this.idx < this.active.length) {
        const e = this.active[this.idx];
        let due = e.tick + this.swingOffsetTicks(e.tick);
        if (due >= len) due = len - 1;
        if (due > tick) break;
        const lane = laneOfChannel(e.ch);
        if (lane < 0 || !this.muteLanes[lane]) {
          if (this.onEvent) this.onEvent(e);
        }
        ++this.idx;
      }
      this.lastFlags = flags;
      return flags;
    }

    laneCounts() {
      const out = [0, 0, 0, 0];
      const arr = (this.state === State.REC && !this.overdub) ? this.inactive : this.active;
      for (const e of arr) {
        if (typeOf(e) !== Type.NOTE_ON) continue;
        const l = laneOfChannel(e.ch);
        if (l >= 0) ++out[l];
      }
      return out;
    }

    laneCount(lane) { return this.laneCounts()[lane] || 0; }

    appendTake(e) {
      if (this.take.length < MAX_TAKE) this.take.push(e);
      else { this.takeFull = true; this.autoStop = true; }
    }

    finishFirst() {
      if (this.inactive.length === 0) {
        this.state = State.STOP;
        this.overdub = false;
        this.phaseQ16 = 0;
        this.phaseTick = 0;
        this.idx = 0;
        return;
      }
      this.inactive.sort(cmpEvent);
      this.active = this.inactive.slice();
      this.inactive = [];
      this.state = State.PLAY;
      this.overdub = false;
      this.phaseQ16 = 0;
      this.phaseTick = 0;
      this.idx = 0;
      this.startPulse = true;
    }

    finishOverdub() {
      // Resume playback immediately; the merge below does not stop the clock.
      this.state = State.PLAY;
      this.overdub = false;
      if (this.take.length === 0) return;

      this.take.sort(cmpEvent);
      const merged = [];
      let i = 0, j = 0;
      while (i < this.active.length || j < this.take.length) {
        if (merged.length >= MAX_EVENTS) { this.overflow = true; break; }
        if (j >= this.take.length ||
            (i < this.active.length && this.active[i].tick <= this.take[j].tick)) {
          merged.push(this.active[i++]);
        } else {
          merged.push(this.take[j++]);
        }
      }
      this.active = merged;
      this.take = [];
      this.idx = this.lowerBound(this.active, this.phaseTick);
      this.state = State.PLAY;
      this.overdub = false;
    }

    lowerBound(arr, tick) {
      let lo = 0, hi = arr.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (arr[mid].tick < tick) lo = mid + 1;
        else hi = mid;
      }
      return lo;
    }
  }

  return { LoopEngine, LoopState: State, LoopEventType: Type, QuantizeGrid: Quant,
           MAX_EVENTS, MAX_TAKE, PPQN, BEATS, AUDIO_RATE,
           typeOf, genOf, laneOfChannel, channelAllowed };
});
