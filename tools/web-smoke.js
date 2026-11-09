#!/usr/bin/env node
// CDP smoke test for the interactive web preview.
// Prereq: Edge/Chrome with --remote-debugging-port=9222 and
//         file:///.../web/index.html?debug=1 open.
//   node tools/web-smoke.js
const http = require('http');

function getJson(url) {
  return new Promise((resolve, reject) => {
    http.get(url, res => {
      let data = '';
      res.on('data', c => { data += c; });
      res.on('end', () => {
        try { resolve(JSON.parse(data)); } catch (e) { reject(e); }
      });
    }).on('error', reject);
  });
}

const sleep = ms => new Promise(r => setTimeout(r, ms));

(async () => {
  const list = await getJson('http://127.0.0.1:9222/json/list');
  const page = list.find(t => t.type === 'page' && t.webSocketDebuggerUrl);
  if (!page) throw new Error('no debuggable page');

  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = reject;
  });

  let nextId = 1;
  const pending = new Map();
  ws.onmessage = ev => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const p = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) p.reject(new Error(msg.error.message));
      else p.resolve(msg.result);
    }
  };

  function send(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = nextId++;
      pending.set(id, { resolve, reject });
      ws.send(JSON.stringify({ id, method, params }));
    });
  }

  async function evaluate(expression) {
    const r = await send('Runtime.evaluate', {
      expression,
      returnByValue: true,
      awaitPromise: true,
    });
    if (r.exceptionDetails) {
      throw new Error(r.exceptionDetails.text || 'JS exception');
    }
    return r.result.value;
  }

  const key = k => `window.dispatchEvent(new KeyboardEvent('keydown',{key:${JSON.stringify(k)},bubbles:true}))`;
  const snap = () => evaluate(`(() => {
    const L = window.__looper;
    if (!L) return null;
    return {
      state: L.engine.state,
      count: L.engine.count(),
      hasLoop: L.engine.hasLoop(),
      bars: L.engine.bars,
      bpmX10: L.engine.bpmX10,
      loopLen: L.engine.loopLenTicks(),
      phase: L.engine.phasePermille(),
      lanes: L.engine.laneCounts(),
      files: L.state.files.length,
    };
  })()`);

  await send('Runtime.enable');

  let ok = true;
  const check = (cond, label, extra = '') => {
    if (!cond) { ok = false; console.log('FAIL  ' + label + (extra ? '  ' + extra : '')); }
    else console.log('PASS  ' + label);
  };

  // Wait for the app's debug hook.
  for (let i = 0; i < 50; ++i) {
    if (await evaluate('!!window.__looper')) break;
    await sleep(100);
  }

  let s = await snap();
  check(!!s, 'debug hook');
  check(s && s.state === 'STOP', 'initial STOP');
  check(s && s.bars === 4 && s.bpmX10 === 1200, 'initial 4 bars / 120 BPM');

  // Record: 4-beat count-in, then the simulated first MIDI note starts REC.
  await evaluate(key('r'));
  await sleep(2600);
  s = await snap();
  check(s && s.state === 'REC', 'R → count-in → REC');
  check(s && s.lanes.some(n => n > 0), 'first-take events captured',
        s ? 'lanes=' + JSON.stringify(s.lanes) : '');
  check(s && s.phase > 0, 'clock is running');

  // 4 bars at 120 BPM = 8 s after the first note; auto-stop wraps to PLAY.
  await sleep(9000);
  s = await snap();
  check(s && s.state === 'PLAY', 'auto-stop at loop end → PLAY');
  check(s && s.hasLoop && s.count > 0, 'loop retains events');
  check(s && s.loopLen === 4 * 4 * 480, 'loop length = 4 bars');

  // Overdub at the current phase, then stop.
  const base = s ? s.count : 0;
  await evaluate(key('r'));
  await sleep(1200);
  s = await snap();
  check(s && s.state === 'REC', 'R → OVERDUB');
  await evaluate(key('r'));
  await sleep(300);
  s = await snap();
  check(s && s.state === 'PLAY', 'R again → PLAY');
  check(s && s.count > base, 'overdub merged events', s ? base + '→' + s.count : '');

  // Save stops and adds a file entry.
  const filesBefore = s ? s.files : 0;
  await evaluate(key('s'));
  await sleep(300);
  s = await snap();
  check(s && s.files === filesBefore + 1, 'S → saved file entry');
  check(s && s.state === 'STOP', 'save stops playback');

  // Clear removes the loop.
  await evaluate(key('c'));
  await sleep(200);
  s = await snap();
  check(s && !s.hasLoop && s.count === 0, 'C → cleared');

  // Early stop keeps the selected bar length.
  await evaluate(key('r'));
  await sleep(2600);
  await evaluate(key('r'));
  await sleep(300);
  s = await snap();
  check(s && s.state === 'PLAY', 'early stop → PLAY');
  check(s && s.loopLen === 4 * 4 * 480, 'early stop keeps 4 bars');

  // Changing bars while a loop exists must be rejected.
  const rejected = await evaluate('window.__looper.changeBars(8) === false');
  check(rejected === true, 'change bars with loop → CLEAR FIRST');

  ws.close();
  console.log('\n' + (ok ? 'RESULT OK' : 'RESULT FAIL'));
  process.exit(ok ? 0 : 1);
})().catch(e => {
  console.error('ERROR ' + (e && e.stack ? e.stack : e));
  process.exit(2);
});
