/* MIDI Looper — 240x135 UI preview.
 * The layout constants here mirror firmware/src/ui.cpp. */
(() => {
  const W = 240, H = 135;
  const C = {
    bg:   '#1d2021', bar: '#282828', panel: '#282828', pad: '#32302f',
    edge: '#504945', line: '#3c3836', txt: '#ebdbb2', lt2: '#d5c4a1',
    dim:  '#a89984', faint: '#7c6f64', acc: '#fe8019', accDim: '#4a2a17',
    amb:  '#fabd2f', aqua: '#8ec07c', blue: '#83a598', purple: '#d3869b',
    red:  '#fb4934', green: '#b8bb26',
  };

  const lcd = document.getElementById('lcd');
  const ctx = lcd.getContext('2d');
  ctx.imageSmoothingEnabled = false;

  const state = {
    tab: 'LOOP',
    loop: 'STOP',          // STOP | ARMED | REC | PLAY
    bars: 4,
    bpm: 120,
    click: true,
    countIn: 1,
    quantize: 3,          // OFF=0, 1/4=1, 1/8=2, 1/16=3, 1/32=4
    swing: 50,
    loopMode: false,
    bar: 1,
    phase: 0,
    selectedLane: 0,
    lanes: [
      { ch: 1,  name: 'LEAD', color: C.acc,  count: 0, on: true, muted: false, flash: 0, level: 0 },
      { ch: 2,  name: 'PAD',  color: C.aqua, count: 0, on: true, muted: false, flash: 0, level: 0 },
      { ch: 3,  name: 'BASS', color: C.amb,  count: 0, on: true, muted: false, flash: 0, level: 0 },
      { ch: 10, name: 'DRUM', color: C.blue, count: 0, on: true, muted: false, flash: 0, level: 0 },
    ],
    files: [
      { name: 'LOOP_0001.MID', size: '12K' },
      { name: 'LOOP_0002.MID', size: '9K'  },
      { name: 'LOOP_0003.MID', size: '21K' },
      { name: 'TAKE_A.MID',    size: '4K'  },
      { name: 'TAKE_B.MID',    size: '7K'  },
      { name: 'IDEA_120.MID',  size: '15K' },
      { name: 'LATE_NIGHT.MID',size: '31K' },
    ],
    selectedFile: 0,
    settings: false,
    settingsRow: 0,
    toast: '',
    toastAt: 0,
    usb: true,
    sd: true,
    battery: 86,
    beatFloat: 0,
    live: true,
  };

  const params = new URLSearchParams(location.search);
  function num(k, d) { const v = parseInt(params.get(k), 10); return Number.isFinite(v) ? v : d; }

  if (params.get('capture') === '1') {
    document.body.classList.add('capture');
    state.live = false;
  }
  if (params.get('tab') === 'file') state.tab = 'FILE';
  if (params.get('settings') === '1') state.settings = true;
  state.bars = [1, 2, 4, 8, 16, 32].includes(num('bars', 4)) ? num('bars', 4) : 4;
  state.bpm = Math.max(40, Math.min(240, num('bpm', 120)));
  state.bar = Math.max(1, Math.min(state.bars, num('bar', 1)));
  state.phase = Math.max(0, Math.min(0.999, parseFloat(params.get('phase') || '0')));

  const demo = params.get('state');
  if (demo === 'rec') {
    state.loop = 'REC';  state.bar = 2; state.phase = 0.42;
    applyCounts([28, 12, 6, 64]);
  } else if (demo === 'play') {
    state.loop = 'PLAY'; state.bar = 3; state.phase = 0.66;
    applyCounts([28, 12, 6, 64]);
  } else if (demo === 'armed') {
    state.loop = 'ARMED'; state.bar = 1; state.phase = 0;
  } else if (demo === 'saved') {
    state.loop = 'PLAY'; state.bar = 4; state.phase = 0.84;
    applyCounts([28, 12, 6, 64]);
    toast('SAVED /MIDILOOP/LOOP_0001.MID');
  }

  function applyCounts(ns) {
    ns.forEach((n, i) => {
      state.lanes[i].count = n;
      state.lanes[i].level = Math.min(1, n / 72);
    });
  }

  function toast(msg) {
    state.toast = msg;
    state.toastAt = performance.now() + 2200;
  }

  // The interactive preview drives the same LoopEngine model as test.html.
  const engine = new LoopEngine();
  engine.setBars(state.bars);
  engine.setBpm(state.bpm * 10);
  engine.setQuantize(state.quantize);
  engine.setSwing(state.swing);
  engine.setLoopMode(state.loopMode);

  let demoAt = 0;
  let demoNext = 0;
  let demoSeq = 0;
  let demoLane = 0;
  const demoNotes = [];

  function syncFromEngine() {
    if (!state.live) return;
    state.loop = engine.state;
    state.bars = engine.bars;
    state.bpm = Math.round(engine.bpmX10 / 10);
    state.loopMode = engine.loopMode;
    const total = engine.loopLenTicks();
    state.phase = total ? Math.min(0.999, engine.phaseTick / total) : 0;
    state.bar = Math.min(state.bars, Math.floor(engine.phaseTick / (480 * 4)) + 1);
    const counts = engine.laneCounts();
    for (let i = 0; i < 4; ++i) {
      state.lanes[i].count = counts[i];
      state.lanes[i].muted = engine.mute(i);
      state.lanes[i].level = Math.min(1, counts[i] / 72);
    }
  }

  function flushDemoNotes(now) {
    for (const n of demoNotes) {
      engine.onMidi(0x80 | n.ch, n.note, 0, now * 1000);
    }
    demoNotes.length = 0;
  }

  function feedDemo(now) {
    if (engine.state === 'ARMED') {
      if (now >= demoAt) {
        // Simulated first MIDI note; this is what starts the take.
        const ch = 0, note = 60;
        engine.onMidi(0x90, note, 100, now * 1000);
        state.lanes[0].flash = performance.now();
        demoNotes.push({ ch, note, until: now + 180 });
        demoAt = Number.POSITIVE_INFINITY;
      }
      return;
    }
    if (engine.state !== 'REC') return;

    if (now >= demoNext) {
      demoNext = now + Math.max(80, 60000 / state.bpm / 2);
      const lane = demoLane++ % 4;
      const ch = [0, 1, 2, 9][lane];
      const base = [60, 64, 48, 36][lane];
      const note = base + (demoSeq++ % 3);
      engine.onMidi(0x90 | ch, note, 90, now * 1000);
      state.lanes[lane].flash = performance.now();
      demoNotes.push({ ch, note, until: now + 140 });
    }

    for (let i = demoNotes.length - 1; i >= 0; --i) {
      if (now >= demoNotes[i].until) {
        engine.onMidi(0x80 | demoNotes[i].ch, demoNotes[i].note, 0, now * 1000);
        demoNotes.splice(i, 1);
      }
    }
  }

  // ------------------------------------------------------------ primitives
  function fill(x, y, w, h, c) {
    ctx.fillStyle = c;
    ctx.fillRect(x | 0, y | 0, Math.max(0, w | 0), Math.max(0, h | 0));
  }
  function frame(x, y, w, h, c) {
    fill(x, y, w, 1, c); fill(x, y + h - 1, w, 1, c);
    fill(x, y, 1, h, c); fill(x + w - 1, y, 1, h, c);
  }
  function hline(x, y, w, c) { fill(x, y, w, 1, c); }
  function vline(x, y, h, c) { fill(x, y, 1, h, c); }

  function glyph(ch, bold) {
    let code = ch.charCodeAt(0);
    if (code >= 97 && code <= 122) code -= 32;
    if (code < 32 || code > 95) code = 63; // '?'
    return (bold ? FONT8B : FONT8)[code - 32];
  }
  function text(x, y, s, c, bold = false, scale = 1) {
    let cx = x;
    ctx.fillStyle = c;
    for (const ch of String(s)) {
      const g = glyph(ch, bold);
      for (let row = 0; row < 8; ++row) {
        const bits = g[row];
        for (let b = 0; b < 6; ++b) {
          if (bits & (1 << b)) ctx.fillRect(cx + b * scale, y + row * scale, scale, scale);
        }
      }
      cx += 6 * scale;
      if (cx > W) break;
    }
  }
  function tw(s, scale = 1) { return String(s).length * 6 * scale; }
  function textR(right, y, s, c, bold = false, scale = 1) { text(right - tw(s, scale), y, s, c, bold, scale); }
  function textC(cx, y, s, c, bold = false, scale = 1) { text(cx - tw(s, scale) / 2, y, s, c, bold, scale); }

  function tri(x, y, w, h, c) {
    for (let i = 0; i < w; ++i) {
      const half = Math.round(h / 2 * (i / Math.max(1, w - 1)));
      fill(x + i, y + Math.round(h / 2) - half, 1, half * 2 + 1, c);
    }
  }

  function battery(x, y, pct) {
    frame(x, y, 14, 7, C.faint);
    fill(x + 14, y + 2, 2, 3, C.faint);
    const n = Math.round(10 * Math.max(0, Math.min(100, pct)) / 100);
    fill(x + 2, y + 2, n, 3, pct > 25 ? C.green : C.red);
  }

  function usbDot(x, y, on) {
    fill(x, y, 5, 5, on ? C.green : C.red);
    fill(x + 1, y + 1, 1, 1, C.bar);
  }
  function sdDot(x, y, on) {
    fill(x, y, 5, 5, on ? C.aqua : C.red);
    fill(x + 1, y + 1, 1, 1, C.bar);
  }

  function folder(x, y, c) {
    fill(x, y + 1, 9, 6, c);
    fill(x, y, 4, 2, c);
    fill(x + 2, y + 3, 5, 1, C.bar);
  }
  function noteIcon(x, y, c) {
    fill(x + 4, y, 1, 8, c);
    fill(x + 4, y, 3, 1, c);
    fill(x + 1, y + 5, 3, 3, c);
    fill(x, y + 6, 1, 1, c);
  }

  // ------------------------------------------------------------ top bar
  function drawTab(label, x, active) {
    fill(x, 2, 42, 9, active ? C.acc : C.pad);
    frame(x, 2, 42, 9, active ? C.acc : C.edge);
    text(x + 4, 3, label, active ? C.bg : C.dim, true);
  }

  function drawTop() {
    fill(0, 0, W, 13, C.bar);
    hline(0, 12, W, C.line);
    drawTab('LOOP', 4, state.tab === 'LOOP');
    drawTab('FILE', 48, state.tab === 'FILE');
    usbDot(150, 5, state.usb);
    text(158, 3, 'USB', C.dim);
    sdDot(184, 5, state.sd);
    text(192, 3, 'SD', C.dim);
    battery(214, 4, state.battery);
  }

  // ------------------------------------------------------------ LOOP page
  function drawStateIcon(x, y) {
    if (state.loop === 'REC') {
      fill(x, y, 8, 8, C.red);
    } else if (state.loop === 'PLAY') {
      tri(x, y, 9, 8, C.green);
    } else if (state.loop === 'ARMED') {
      frame(x, y, 8, 8, C.amb);
      fill(x + 2, y + 2, 4, 4, C.amb);
    } else {
      fill(x, y, 8, 8, C.faint);
    }
  }

  function drawLane(i, y) {
    const L = state.lanes[i];
    const sel = state.selectedLane === i;
    const muted = !!L.muted;
    const flash = L.flash && performance.now() - L.flash < 90;
    if (sel) {
      fill(2, y, 236, 17, C.pad);
      fill(2, y, 2, 17, L.color);
    }
    if (flash) {
      fill(2, y, 236, 17, C.pad);
      hline(2, y + 16, 236, L.color);
    }
    text(7, y + 5, 'CH' + L.ch, flash ? C.txt : (muted ? C.faint : L.color), true);
    text(37, y + 5, L.name, flash ? C.txt : (sel ? C.txt : C.dim), sel);

    // 18-cell meter
    const mx = 68, my = y + 5, cw = 5, cg = 1;
    fill(mx, my, 18 * (cw + cg) - cg, 6, C.line);
    const lit = Math.round((L.level || 0) * 18);
    for (let s = 0; s < 18; ++s) {
      const on = flash ? C.txt : (muted ? C.faint : L.color);
      fill(mx + s * (cw + cg), my, cw, 6, s < lit ? on : C.line);
    }

    textR(204, y + 5, L.count ? String(L.count) : '--', C.dim);
    text(217, y + 5, muted ? 'OFF' : 'ON', muted ? C.red : C.aqua);
  }

  function drawLoop() {
    const y = 17;
    drawStateIcon(7, y);
    const label = state.loop === 'REC' ? 'REC' : state.loop === 'PLAY' ? 'PLAY'
                : state.loop === 'ARMED' ? 'ARMED' : 'STOP';
    const col = state.loop === 'REC' ? C.red : state.loop === 'PLAY' ? C.green
              : state.loop === 'ARMED' ? C.amb : C.faint;
    text(20, y + 1, label, col, true);
    if (state.loopMode) text(68, y + 1, 'LOOP', C.amb, true);

    text(96, y + 1, 'BAR ' + state.bar + '/' + state.bars, C.txt, true);
    textR(234, y + 1, state.bpm + ' BPM', C.dim);

    // progress / beat ruler
    const px = 6, py = 31, pw = 228, ph = 7;
    frame(px - 1, py - 1, pw + 2, ph + 2, C.edge);
    fill(px, py, pw, ph, C.line);
    if (state.loop === 'REC') fill(px, py, Math.round(pw * state.phase), ph, C.red);
    else if (state.loop === 'PLAY') fill(px, py, Math.round(pw * state.phase), ph, C.acc);
    else if (state.lanes.some(l => l.count)) fill(px, py, Math.round(pw * state.phase), ph, C.accDim);

    const beats = state.bars * 4;
    for (let i = 0; i <= beats; ++i) {
      const x = px + Math.round(i * pw / beats);
      const bar = (i % 4) === 0;
      fill(x, py, 1, bar ? ph : 3, bar ? C.faint : C.bg);
    }
    const hx = px + Math.round(pw * state.phase);
    fill(hx - 1, py - 2, 2, ph + 4, C.txt);

    for (let i = 0; i < 4; ++i) drawLane(i, 44 + i * 18);

    // bottom hints
    fill(0, 124, W, 11, C.bar);
    hline(0, 123, W, C.line);
    let hint;
    if (state.loop === 'ARMED') hint = 'WAIT MIDI...   R CANCEL';
    else if (state.loop === 'REC') hint = 'R STOP   ENT PLAY   S SAVE';
    else if (state.loop === 'PLAY') hint = 'R OVERDUB   ENT STOP   S SAVE';
    else hint = 'R REC   ENT PLAY   S SAVE';
    text(6, 125, hint, C.faint);
    textR(234, 125, engine.hasLoop() ? 'U UNDO' : 'TAB FILE', C.faint);
  }

  // ------------------------------------------------------------ FILE page
  function drawFiles() {
    fill(0, 13, W, 11, C.panel);
    hline(0, 24, W, C.line);
    folder(5, 16, C.amb);
    text(17, 16, '/MIDILOOP', C.blue, true);
    textR(234, 16, state.files.length + ' FILES', C.faint);

    if (!state.files.length) {
      textC(W / 2, 58, 'NO .MID FILES', C.faint, true);
      textC(W / 2, 71, 'PUT FILES IN /MIDILOOP', C.faint);
    } else {
      const y0 = 27;
      for (let i = 0; i < 6 && i < state.files.length; ++i) {
        const f = state.files[i];
        const y = y0 + i * 16;
        const sel = i === state.selectedFile;
        if (sel) fill(0, y, W, 16, C.acc);
        noteIcon(6, y + 4, sel ? C.bg : C.amb);
        let name = f.name;
        if (name.length > 26) name = name.slice(0, 25) + '~';
        text(18, y + 4, name, sel ? C.bg : C.txt, sel);
        textR(230, y + 4, f.size, sel ? C.bg : C.faint);
      }
    }

    fill(0, 124, W, 11, C.bar);
    hline(0, 123, W, C.line);
    text(6, 125, 'D DEL   N RENAME', C.faint);
    textR(234, 125, 'TAB LOOP', C.faint);
  }

  // ------------------------------------------------------------ settings
  function drawSettings() {
    fill(0, 13, W, 111, C.bg);
    frame(28, 16, 184, 104, C.edge);
    fill(29, 17, 182, 102, C.panel);
    textC(120, 21, 'SETTINGS', C.amb, true);

    const qnames = ['OFF', '1/4', '1/8', '1/16', '1/32'];
    const rows = [
      ['BPM', String(state.bpm)],
      ['BARS', String(state.bars)],
      ['QUANT', qnames[state.quantize] || '1/16'],
      ['SWING', String(state.swing)],
      ['CLICK', state.click ? 'ON' : 'OFF'],
      ['COUNT-IN', state.countIn + ' BAR'],
    ];
    for (let i = 0; i < rows.length; ++i) {
      const y = 32 + i * 12;
      if (i === state.settingsRow) {
        fill(32, y - 2, 176, 12, C.pad);
        fill(32, y - 2, 2, 12, C.amb);
      }
      text(36, y, rows[i][0], i === state.settingsRow ? C.amb : C.dim, i === state.settingsRow);
      textR(176, y, rows[i][1], C.txt, true);
      text(184, y, '< >', C.faint);
    }
    textC(120, 108, 'UP/DN   <-/->   ESC', C.faint);
  }

  function drawToast() {
    if (!state.toast || performance.now() > state.toastAt) return;
    const t = state.toast;
    const w = tw(t) + 12;
    const x = Math.round((W - w) / 2), y = 108;
    fill(x, y, w, 12, C.amb);
    text(x + 6, y + 2, t, C.bg, true);
  }

  // ------------------------------------------------------------ frame
  function draw() {
    fill(0, 0, W, H, C.bg);
    drawTop();
    if (state.tab === 'FILE') drawFiles();
    else drawLoop();
    if (state.settings) drawSettings();
    drawToast();
  }

  // ------------------------------------------------------------ animation
  let last = performance.now();
  function tick(now) {
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;

    if (state.live) {
      engine.nowMs = now;
      if (engine.state === 'REC' && engine.autoStop) flushDemoNotes(now);
      engine.service(now);
      feedDemo(now);              // ARMED: inject the simulated first note
      if (engine.state === 'REC' || engine.state === 'PLAY') {
        const frames = Math.max(1, Math.round(dt * 44100));
        engine.pump(frames);
      }
      syncFromEngine();
    }

    draw();
    syncPanel();
    requestAnimationFrame(tick);
  }

  // ------------------------------------------------------------ controls
  function syncPanel() {
    const $ = id => document.getElementById(id);
    if (!$('stateOut')) return;
    $('stateOut').textContent = state.loop;
    $('barOut').textContent = state.bar + ' / ' + state.bars;
    $('bpmOut').textContent = state.bpm;
    $('barsOut').textContent = state.bars;
    $('countOut').textContent = state.countIn + ' BAR';
    $('btnRec').classList.toggle('on', state.loop === 'REC' || state.loop === 'ARMED');
    $('btnPlay').classList.toggle('on', state.loop === 'PLAY');
    $('btnClick').classList.toggle('on', state.click);
    $('btnClick').textContent = state.click ? 'ON' : 'OFF';
    $('btnLoop').classList.toggle('on', state.tab === 'LOOP');
    $('btnFile').classList.toggle('on', state.tab === 'FILE');
  }

  function startRec() {
    const now = performance.now();
    engine.nowMs = now;
    if (engine.state === 'REC' || engine.state === 'ARMED') {
      stopRec();
      return;
    }

    flushDemoNotes(now);

    if (engine.hasLoop()) {
      engine.startOverdub();
      demoAt = now;
      demoNext = now;
      toast('OVERDUB');
    } else {
      engine.startFirst();
      demoAt = now + (state.countIn ? 4 * 60000 / state.bpm : 0);
      demoNext = now;
      toast(state.countIn ? 'COUNT IN' : 'WAIT MIDI...');
    }
    syncFromEngine();
  }

  function stopRec() {
    const now = performance.now();
    engine.nowMs = now;
    flushDemoNotes(now);
    engine.stopRecord();
    syncFromEngine();
    toast(engine.state === 'PLAY' ? 'LOOP ' + state.bars + ' BAR' : 'CANCELED');
  }

  function togglePlay() {
    const now = performance.now();
    engine.nowMs = now;
    flushDemoNotes(now);

    if (engine.state === 'PLAY') {
      engine.stop();
      toast('STOPPED');
    } else if (engine.state === 'REC' || engine.state === 'ARMED') {
      engine.stopRecord();
      toast(engine.state === 'PLAY' ? 'LOOP ' + state.bars + ' BAR' : 'STOPPED');
    } else if (!engine.play()) {
      toast('REC FIRST');
    } else {
      toast('PLAYING');
    }
    syncFromEngine();
  }

  function save() {
    const now = performance.now();
    engine.nowMs = now;
    flushDemoNotes(now);
    if (engine.state === 'REC' || engine.state === 'ARMED') engine.stopRecord();
    if (!engine.hasLoop()) {
      toast('NOTHING TO SAVE');
      return;
    }
    engine.stop();
    syncFromEngine();
    const name = 'LOOP_' + String(state.files.length + 1).padStart(4, '0') + '.MID';
    state.files.unshift({ name, size: '6K' });
    toast('SAVED ' + name);
  }

  function delFile() {
    if (!state.files.length) return;
    state.files.splice(state.selectedFile, 1);
    state.selectedFile = Math.min(state.selectedFile, Math.max(0, state.files.length - 1));
    toast('DELETED');
  }

  function renameFile() {
    const f = state.files[state.selectedFile];
    if (!f) return;
    f.name = f.name.replace(/\.MID$/i, '_2.MID');
    toast('RENAMED');
  }

  function changeBars(n) {
    if (engine.state === 'REC' || engine.state === 'ARMED') { toast('RECORDING'); return false; }
    if (engine.hasLoop()) { toast('CLEAR FIRST'); return false; }
    engine.setBars(n);
    state.bars = engine.bars;
    state.bar = 1;
    state.phase = 0;
    return true;
  }

  function changeBpm(v) {
    if (engine.state === 'REC' || engine.state === 'ARMED') { toast('RECORDING'); return false; }
    state.bpm = Math.max(40, Math.min(240, Math.round(v)));
    engine.setBpm(state.bpm * 10);
    return true;
  }

  function changeQuantize(delta) {
    state.quantize = (state.quantize + delta + 5) % 5;
    engine.setQuantize(state.quantize);
    const names = ['OFF', '1/4', '1/8', '1/16', '1/32'];
    toast('QUANT ' + names[state.quantize]);
  }

  function changeSwing(delta) {
    state.swing = Math.max(50, Math.min(75, state.swing + delta * 5));
    engine.setSwing(state.swing);
    toast('SWING ' + state.swing);
  }

  function adjust(delta) {
    if (state.settings) {
      if (state.settingsRow === 0) {
        changeBpm(state.bpm + delta * 5);
      } else if (state.settingsRow === 1) {
        const opts = [1, 2, 4, 8, 16, 32];
        const i = opts.indexOf(state.bars);
        changeBars(opts[Math.max(0, Math.min(opts.length - 1, i + delta))]);
      } else if (state.settingsRow === 2) {
        changeQuantize(delta);
      } else if (state.settingsRow === 3) {
        changeSwing(delta);
      } else if (state.settingsRow === 4) {
        state.click = !state.click;
      } else {
        state.countIn = state.countIn ? 0 : 1;
      }
    } else if (state.tab === 'LOOP') {
      changeBpm(state.bpm + delta);
    }
  }

  window.addEventListener('keydown', e => {
    const k = e.key;
    if (k === 'Tab') { e.preventDefault(); state.tab = state.tab === 'LOOP' ? 'FILE' : 'LOOP'; return; }
    if (k === 'Escape') { state.settings = false; return; }
    if (k === 'r' || k === 'R' || k === ' ') { e.preventDefault(); startRec(); return; }
    if (k === 'Enter') { e.preventDefault(); togglePlay(); return; }
    if (k === 's' || k === 'S') { save(); return; }
    if (k === 'c' || k === 'C') {
      demoNotes.length = 0;
      engine.clear();
      syncFromEngine();
      toast('CLEARED');
      return;
    }
    if (k === 'g' || k === 'G') { state.settings = !state.settings; return; }
    if (k === 'l' || k === 'L') {
      const on = engine.toggleLoopMode();
      if (on) {
        if (engine.state === 'REC' || engine.state === 'ARMED') {
          toast('LOOP REC');
        } else if (engine.hasLoop()) {
          startRec();
          toast('LOOP REC');
        } else {
          startRec();
          toast('LOOP ARM');
        }
      } else {
        if (engine.state === 'REC' || engine.state === 'ARMED') stopRec();
        toast('LOOP OFF');
      }
      syncFromEngine();
      return;
    }
    if (k === 'm' || k === 'M') { state.click = !state.click; toast(state.click ? 'CLICK ON' : 'CLICK OFF'); return; }
    if (k === 'u' || k === 'U') {
      const now = performance.now();
      engine.nowMs = now;
      flushDemoNotes(now);
      const ok = engine.undo();
      syncFromEngine();
      toast(ok ? 'UNDO' : 'NOTHING TO UNDO');
      return;
    }
    if (k >= '1' && k <= '4') {
      const lane = Number(k) - 1;
      if (e.shiftKey) {
        const muted = engine.toggleMute(lane);
        syncFromEngine();
        toast('CH' + [1, 2, 3, 10][lane] + (muted ? ' MUTE' : ' ON'));
      } else {
        state.selectedLane = lane;
      }
      return;
    }
    if (k === 'ArrowUp') {
      e.preventDefault();
      if (state.settings) state.settingsRow = (state.settingsRow + 5) % 6;
      else changeBars(state.bars * 2);
      return;
    }
    if (k === 'ArrowDown') {
      e.preventDefault();
      if (state.settings) state.settingsRow = (state.settingsRow + 1) % 6;
      else changeBars(state.bars / 2);
      return;
    }
    if (k === 'ArrowLeft') { e.preventDefault(); adjust(-1); return; }
    if (k === 'ArrowRight') { e.preventDefault(); adjust(+1); return; }
    if (k === 'd' || k === 'D') { if (state.tab === 'FILE') delFile(); return; }
    if (k === 'n' || k === 'N') { if (state.tab === 'FILE') renameFile(); return; }
  });

  // panel bindings
  const on = (id, fn) => { const el = document.getElementById(id); if (el) el.addEventListener('click', fn); };
  on('btnRec', startRec);
  on('btnPlay', togglePlay);
  on('btnStop', () => {
    const now = performance.now();
    engine.nowMs = now;
    flushDemoNotes(now);
    engine.stop();
    syncFromEngine();
    toast('STOPPED');
  });
  on('btnLoop', () => { state.tab = 'LOOP'; });
  on('btnFile', () => { state.tab = 'FILE'; });
  on('btnSettings', () => { state.settings = !state.settings; });
  on('btnTest', () => { window.open('test.html', '_blank'); });
  on('btnClick', () => { state.click = !state.click; });
  const bpmEl = document.getElementById('bpm');
  if (bpmEl) bpmEl.addEventListener('input', e => { changeBpm(Number(e.target.value)); });
  document.querySelectorAll('#barBtns button').forEach(b => {
    b.addEventListener('click', () => {
      if (!changeBars(Number(b.dataset.bars))) return;
      document.querySelectorAll('#barBtns button').forEach(x => x.classList.toggle('on', x === b));
    });
  });

  // Debug hook for the CDP-driven web smoke test.
  if (params.get('debug') === '1') {
    window.__looper = {
      engine,
      state,
      startRec,
      stopRec,
      togglePlay,
      save,
      changeBars,
      changeBpm,
      changeQuantize,
      changeSwing,
      syncFromEngine,
    };
  }

  // initial frame
  requestAnimationFrame(tick);
})();
