/* Micro-benchmark: raw BrainV2.step() cost for MCU-sizing (finding 35).
   Times learning and read-only steps for representative configs, and
   counts the memory the live arrays actually need (vs the 1024 cap). */
'use strict';
const { loadBench } = require('./headless');
const w = loadBench();

// grab the class via a live brain: select a scenario, then use constructor
w.bench.run('balance', 1, { seed: 0 });
// reach the constructor through the harness-created brain: rebuild configs
const mk = (cfg) => {
  // clone the class through the prototype of the current brain
  const proto = Object.getPrototypeOf(getBrain());
  return new proto.constructor(cfg);
};
// the harness keeps `brain` in closure; expose via a bench run + trick:
// simplest reliable route: run a scenario matching each config instead.
function getBrain() { return w.__lastBrain; }

// Patch: expose the brain by running and reading via bench internals is not
// available; instead re-derive the class from a fresh evaluation:
const fs = require('fs');
const path = require('path');
const html = fs.readFileSync(path.join(__dirname, '..', 'ui', 'testbench.html'), 'utf8');
const src = html.slice(html.indexOf('<script>') + 8, html.lastIndexOf('</script>'));
// extract just what we need by evaluating the whole script again with a
// hook that captures BrainV2 via the SCENARIOS registry step calls: easier
// to just eval the brain classes: find class boundaries.
const start = src.indexOf('const FAN=4;');
const end = src.indexOf("BrainV2._topBuf=new Float64Array(0);") + 'BrainV2._topBuf=new Float64Array(0);'.length;
const brainSrc = src.slice(start, end);
const preamble = src.slice(0, src.indexOf('const FAN=4;'));
const factory = new Function('window', preamble + brainSrc + '; return {BrainV2};');
const { BrainV2 } = factory({ BENCHSEED: 0 });

function timeConfig(name, cfg, nCtxFill, learn) {
  const b = new BrainV2(cfg);
  b.lastGrowth = 1e15;   // pin capacity at n0 (MCU builds fix capacity too)
  const ctx = new Float64Array(cfg.nCtx);
  const tch = new Float64Array(cfg.nOut);
  /* LEARNABLE teacher (a smooth function of ctx) so the residual whitens
     and the growth trigger stays quiet — otherwise the benchmark times a
     1024-unit runaway brain no scenario ever reaches. */
  const fill = (i) => {
    for (let c = 0; c < cfg.nCtx; c++) ctx[c] = Math.sin(i * 0.01 * (c + 1));
    for (let j = 0; j < cfg.nOut; j++)
      tch[j] = 0.4 * ctx[0] * ctx[1 % cfg.nCtx] + 0.2 * ctx[2 % cfg.nCtx] - b.out[j];
  };
  for (let i = 0; i < 5000; i++) { fill(i); b.step(ctx, tch, learn); }
  const N = 50000;
  const t0 = process.hrtime.bigint();
  for (let i = 0; i < N; i++) { fill(i); b.step(ctx, tch, learn); }
  const ns = Number(process.hrtime.bigint() - t0) / N;
  console.log(`${name.padEnd(34)} ${(ns / 1000).toFixed(2)} us/step  (n=${b.n}, active=${(b.active * 100).toFixed(0)}%)`);
  return ns;
}

console.log('BrainV2 step cost, Node ' + process.version + ' (x64 desktop):');
timeConfig('control n0=64 nCtx=8 J=1 LEARN', { nCtx: 8, nOut: 1, dt: 0.02, authority: 1, n0: 64, osc: 0 }, 8, true);
timeConfig('control n0=64 nCtx=8 J=1 read', { nCtx: 8, nOut: 1, dt: 0.02, authority: 1, n0: 64, osc: 0 }, 8, false);
timeConfig('control n0=64 nCtx=8 J=1 NLMS LEARN', { nCtx: 8, nOut: 1, dt: 0.02, authority: 1, n0: 64, osc: 0, rule: 'nlms' }, 8, true);
timeConfig('control n0=64 +3osc J=2 LEARN', { nCtx: 12, nOut: 2, dt: 0.01, authority: 50, n0: 64 }, 12, true);
timeConfig('control n0=128 nCtx=12 J=2 LEARN', { nCtx: 12, nOut: 2, dt: 0.01, authority: 50, n0: 128 }, 12, true);
timeConfig('decision n0=320 nCtx=13 J=8 read', { nCtx: 13, nOut: 8, dt: 0.1, authority: 6, n0: 320, gate: false, tauOut: 0 }, 13, false);

// memory audit for a control build (live arrays, ignoring the 1024 cap)
function memAudit(n, nCtx, J, rule) {
  const f64 = 8, i32 = 4;
  const basis = n * 4 * (i32 + f64) + n * f64;         // idx + wIn + bias
  const weights = 2 * J * n * f64;                      // wf + ws
  const autostep = rule === 'nlms' ? 0 : 3 * J * n * f64; // beta,h,v
  const work = (3 * n + 2 * nCtx + 2 * nCtx) * f64;     // elig,sAct,prevS,mu,var,xn...
  const gate = 8 * J * f64 + 64;
  return { basis, weights, autostep, work: work + gate,
    totalKB: +((basis + weights + autostep + work + gate) / 1024).toFixed(1) };
}
console.log('\nfloat64 JS memory (n=64,nCtx=8,J=1):', JSON.stringify(memAudit(64, 8, 1, 'autostep')));
console.log('same but NLMS rule (no beta/h/v):  ', JSON.stringify(memAudit(64, 8, 1, 'nlms')));
