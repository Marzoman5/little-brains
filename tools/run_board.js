/* One-command board runner (the CI instrument).

   Usage:
     node tools/run_board.js                  # full official board, 10 seeds
     node tools/run_board.js --quick          # seed-0 smoke board
     node tools/run_board.js --out results/board.json
     node tools/run_board.js --brain v1       # V1 reference board

   Exit code 1 if any job errors or any brain NaN-guard trips. */
'use strict';
const fs = require('fs');
const { loadBench } = require('./headless');

const args = process.argv.slice(2);
const opt = (name, dflt) => {
  const i = args.indexOf('--' + name);
  return i >= 0 ? args[i + 1] : dflt;
};
const quick = args.includes('--quick');
const brainV = opt('brain', 'v2');
const outPath = opt('out', null);
const seeds = quick ? [0] : [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];

const w = loadBench(opt('file', null));
const bench = w.bench;

const jobs = [];
for (const sc of ['balance', 'path', 'car', 'flight', 'rover', 'launcher', 'maze'])
  for (const s of seeds)
    jobs.push({ fn: 'run', args: [sc, 240, { seed: s, brain: brainV }], tag: sc });
for (const s of seeds)
  jobs.push({ fn: 'runBlocks', args: ['fight', 500, 3, { seed: s, brain: brainV }], tag: 'fight' });
for (const s of seeds)
  jobs.push({ fn: 'runBlocks', args: ['hive', 200, 3, { seed: s, brain: brainV }], tag: 'hive' });
if (brainV === 'v2')
  for (const s of seeds)
    jobs.push({ fn: 'runBlocks', args: ['gossip', 200, 3, { seed: s, brain: brainV }], tag: 'gossip' });

const results = [];
let failed = false;
const t0 = Date.now();
for (const j of jobs) {
  const tj = Date.now();
  let r;
  try {
    r = bench[j.fn](...j.args);
  } catch (e) {
    r = { error: String(e && e.stack || e), job: j };
    failed = true;
  }
  r.tag = j.tag;
  r.wallMs = Date.now() - tj;
  const nan = 'blocks' in r ? r.blocks.some(b => b.nan > 0) : r.nan > 0;
  if (nan) failed = true;
  results.push(r);
  process.stderr.write(`${j.tag} seed=${r.seed} ${'blocks' in r
    ? 'lastPct=' + r.blocks[r.blocks.length - 1].pct
    : 'pct=' + r.pct}${nan ? ' NAN!' : ''}\n`);
}

const stats = {};
for (const r of results) {
  if (r.error) continue;
  const pct = 'blocks' in r ? r.blocks[r.blocks.length - 1].pct : r.pct;
  (stats[r.tag] = stats[r.tag] || []).push(pct);
}
const lines = [];
for (const tag of Object.keys(stats).sort()) {
  const v = stats[tag];
  const m = v.reduce((a, b) => a + b, 0) / v.length;
  const sd = v.length > 1
    ? Math.sqrt(v.reduce((a, b) => a + (b - m) * (b - m), 0) / (v.length - 1))
    : 0;
  lines.push(`${tag.padEnd(10)} ${m.toFixed(1).padStart(6)} +- ${sd.toFixed(1).padStart(5)}  n=${v.length}`);
}
console.log(`board (${brainV}, ${seeds.length} seed${seeds.length > 1 ? 's' : ''}, ${((Date.now() - t0) / 1000).toFixed(1)}s)`);
console.log(lines.join('\n'));

if (outPath) {
  fs.writeFileSync(outPath, JSON.stringify(results));
  console.log('raw results -> ' + outPath);
}
process.exit(failed ? 1 : 0);
