/* Library smoke test: allocation honesty, learning sanity, save/load
   round-trip, determinism, outcome watchdog. Exit 1 on any failure. */
'use strict';
const LB = require('../lib/littlebrains.js');
let fails = 0;
const ok = (cond, name) => {
  console.log((cond ? 'PASS ' : 'FAIL ') + name);
  if (!cond) fails++;
};

// 1. allocation honesty
const b16 = new LB.Brain({ nCtx: 6, nOut: 1, dt: 0.02, authority: 1, n: 16, seed: 3 });
ok(b16.wf[0].length === 16 && b16.idx.length === 64, 'n=16 allocates 16-unit arrays');

// 2. learning sanity (gate off — tests the learner, not the gate):
//    residual-demand teaching on an instant plant
function trainRun(brain, steps) {
  const ctx = new Float64Array(6);
  let errAcc = 0, cnt = 0;
  for (let i = 0; i < steps; i++) {
    const ph = i * 0.013;
    for (let c = 0; c < 6; c++) ctx[c] = LB._softmath.sin(((ph * (c + 1)) % 6.283185307179586) - 3.141592653589793);
    const target = 0.6 * ctx[0] + 0.3 * ctx[1] * ctx[2];
    const teach = target - brain.out[0];      // residual demand
    brain.step(ctx, teach, true);
    if (i > steps - 500) { const e = target - brain.corr[0]; errAcc += e * e; cnt++; }
  }
  return Math.sqrt(errAcc / cnt);
}
const bA = new LB.Brain({ nCtx: 6, nOut: 1, dt: 0.02, authority: 2, n: 16, seed: 3, gate: false });
const rms = trainRun(bA, 6000);
ok(rms < 0.35, 'n=16 halves a hard target (rms ' + rms.toFixed(4) + ' < 0.35, target std ~0.46)');

// 2b. the gate in its real habitat: a closed FEL loop. First-order plant,
//     P-controller host, context-locked disturbance the brain can learn.
//     Teacher = the host's own output; gate must ramp open, and the loop
//     with the brain must beat the plain loop.
function felLoop(useBrain) {
  const b = useBrain ? new LB.Brain({ nCtx: 3, nOut: 1, dt: 0.02, authority: 4, n: 16, seed: 3 }) : null;
  const ctx = new Float64Array(3);
  let y = 0, errAcc = 0, cnt = 0;
  for (let i = 0; i < 15000; i++) {
    const ph = LB._softmath.sin(((i * 0.02 * 1.3) % 6.283185307179586) - 3.141592653589793);
    const ph2 = LB._softmath.cos(((i * 0.02 * 1.3) % 6.283185307179586) - 3.141592653589793);
    const ref = 0.5 * ph;
    const dist = 1.6 * ph2 + 0.8;                 // context-locked load
    const upid = 3.0 * (ref - y);
    let u = upid;
    if (b) { ctx[0] = ref; ctx[1] = y; ctx[2] = ph2; u += b.step(ctx, upid, true)[0]; }
    y += 0.02 * 4.0 * (u + dist - y);             // first-order plant
    if (i > 12000) { const e = ref - y; errAcc += e * e; cnt++; }
  }
  return { rms: Math.sqrt(errAcc / cnt), alpha: b ? b.meanAlpha() : 0 };
}
const plain = felLoop(false), gated = felLoop(true);
ok(gated.alpha > 0.9, 'gate opens in a real FEL loop (alpha ' + gated.alpha.toFixed(2) + ')');
ok(gated.rms < 0.5 * plain.rms, 'brain beats plain loop (' + gated.rms.toFixed(4) + ' vs ' + plain.rms.toFixed(4) + ')');

// 3. determinism: same seed twice -> identical weights
const bB = new LB.Brain({ nCtx: 6, nOut: 1, dt: 0.02, authority: 2, n: 16, seed: 3 });
trainRun(bB, 6000);
let same = true;
for (let i = 0; i < 16; i++) if (bA.ws[0][i] !== bB.ws[0][i] || bA.wf[0][i] !== bB.wf[0][i]) same = false;
ok(same, 'bit-identical across two runs (same seed)');

// 4. save/load round-trip
const blob = bA.save(true);
const bC = new LB.Brain({ nCtx: 6, nOut: 1, dt: 0.02, authority: 2, n: 16, seed: 3 });
bC.load(blob);
let match = true;
for (let i = 0; i < 16; i++) if (bC.ws[0][i] !== bA.ws[0][i] || bC.wf[0][i] !== bA.wf[0][i]) match = false;
ok(match, 'save/load round-trip preserves weights (' + blob.length + ' bytes)');

// 5. load rejects wrong seed and corrupt blobs
let threw = 0;
try { new LB.Brain({ nCtx: 6, nOut: 1, dt: 0.02, authority: 2, n: 16, seed: 4 }).load(blob); } catch (e) { threw++; }
const bad = Uint8Array.from(blob); bad[40] ^= 0xFF;
try { bC.load(bad); } catch (e) { threw++; }
ok(threw === 2, 'load rejects wrong seed + corrupt CRC');

// 6. outcome watchdog: harmful costs cap authority
const bD = new LB.Brain({ nCtx: 6, nOut: 1, dt: 0.02, authority: 2, n: 16, seed: 3, gate: false });
for (let i = 0; i < 8; i++) bD.outcome(1.0);   // anchor ~1.0
for (let i = 0; i < 30; i++) bD.outcome(3.0);  // 3x worse
ok(bD.oCap < 0.3, 'outcome watchdog caps authority (oCap ' + bD.oCap.toFixed(2) + ')');

// 7. softmath sanity vs native (loose tolerance; determinism is the point)
const sm = LB._softmath;
ok(Math.abs(sm.tanh(0.8) - Math.tanh(0.8)) < 1e-6, 'softTanh ~ tanh');
ok(Math.abs(sm.exp(-3.7) - Math.exp(-3.7)) / Math.exp(-3.7) < 1e-8, 'softExp ~ exp');
ok(Math.abs(sm.sin(1.1) - Math.sin(1.1)) < 1e-7, 'softSin ~ sin');
ok(Math.abs(sm.log(0.05) - Math.log(0.05)) < 1e-9, 'softLog ~ log');

process.exit(fails ? 1 : 0);
