/* Golden-vector generator: runs the JS reference library through three
   deterministic drivers and dumps every step's corr outputs + final
   weights as binary f64 (little-endian). The C test harness replays the
   SAME drivers and must match bit-for-bit.
   Format: u32 magic 0x4C425456 | u32 nConfigs | per config:
     u32 steps | u32 J | u32 n | f64 corr[steps*J] | f64 wf[J*n] |
     f64 ws[J*n] */
'use strict';
const fs = require('fs');
const path = require('path');
const LB = require('../lib/littlebrains.js');
const sm = LB._softmath;

const chunks = [];
function f64s(arr) { const b = Buffer.alloc(arr.length * 8); arr.forEach((v, i) => b.writeDoubleLE(v, i * 8)); return b; }
function u32(v) { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0, 0); return b; }

function dump(brain, corrLog) {
  const J = brain.nOut, n = brain.n;
  chunks.push(u32(corrLog.length / J), u32(J), u32(n));
  chunks.push(f64s(corrLog));
  const wf = [], ws = [];
  for (let j = 0; j < J; j++) for (let i = 0; i < n; i++) { wf.push(brain.wf[j][i]); ws.push(brain.ws[j][i]); }
  chunks.push(f64s(wf), f64s(ws));
}

/* config A: closed FEL loop — autostep, 3 oscillators, golgi, gate,
   growth enabled (cap > n) */
{
  const b = new LB.Brain({ nCtx: 3, nOut: 1, dt: 0.02, authority: 4, n: 16, cap: 32, seed: 3 });
  const ctx = new Float64Array(3), log = [];
  let y = 0;
  for (let i = 0; i < 4000; i++) {
    const ph = sm.sin(((i * 0.02 * 1.3) % 6.283185307179586) - 3.141592653589793);
    const ph2 = sm.cos(((i * 0.02 * 1.3) % 6.283185307179586) - 3.141592653589793);
    const ref = 0.5 * ph;
    const dist = 1.6 * ph2 + 0.8;
    const upid = 3.0 * (ref - y);
    ctx[0] = ref; ctx[1] = y; ctx[2] = ph2;
    const corr = b.step(ctx, upid, true);
    log.push(corr[0]);
    const u = upid + corr[0];
    y += 0.02 * 4.0 * (u + dist - y);
  }
  dump(b, log);
}

/* config B: episodic decisions — NLMS, replay ring, masked outputs,
   outcome watchdog exercised */
{
  const b = new LB.Brain({ nCtx: 4, nOut: 2, dt: 0.5, authority: 2, n: 24, seed: 7 });
  const ctx = new Float64Array(4), tch = new Float64Array(2), log = [];
  for (let i = 0; i < 1500; i++) {
    for (let c = 0; c < 4; c++)
      ctx[c] = sm.sin(((i * (0.37 + 0.11 * c)) % 6.283185307179586) - 3.141592653589793);
    tch[0] = 0.5 * ctx[0] * ctx[1] - b.out[0];
    tch[1] = (i % 3 === 0) ? 0 : 0.3 * ctx[2] - b.out[1];  // masked sometimes
    const corr = b.step(ctx, tch, true);
    log.push(corr[0], corr[1]);
    if (i % 10 === 9) {
      const c0 = tch[0] < 0 ? -tch[0] : tch[0];
      b.outcome(c0);
    }
  }
  dump(b, log);
}

/* config C: kwta sparsifier, no oscillators, NLMS in continuous mode */
{
  const b = new LB.Brain({ nCtx: 5, nOut: 1, dt: 0.01, authority: 1, n: 32, seed: 11, sparsifier: 'kwta', osc: 0, rule: 'nlms' });
  const ctx = new Float64Array(5), log = [];
  for (let i = 0; i < 3000; i++) {
    for (let c = 0; c < 5; c++)
      ctx[c] = sm.sin(((i * 0.01 * (1 + 0.7 * c)) % 6.283185307179586) - 3.141592653589793);
    const t = 0.4 * ctx[0] + 0.2 * ctx[3] - b.out[0];
    log.push(b.step(ctx, t, true)[0]);
  }
  dump(b, log);
}

const out = Buffer.concat([u32(0x4C425456), u32(3), ...chunks]);
const dst = path.join(__dirname, '..', 'lib', 'c', 'vectors.bin');
fs.writeFileSync(dst, out);
console.log('wrote ' + out.length + ' bytes -> ' + dst);
