# Architecture reference — current state (V2.1, 2026-08-26)

The single source of truth is `ui/testbench.html` (one self-contained file:
two brain classes + nine scenarios + harness). The Python library `cereb/`
is V1-era with partial back-ports and is NOT current — treat it as legacy
until someone back-ports BrainV2 or formally deprecates it.

## Files

- `ui/testbench.html` — THE artifact. Brain (V1), BrainV2, 9 scenarios,
  harness, bench API. Published at
  https://claude.ai/code/artifact/98ac362a-9080-4535-9bf6-f9e41f060d4c
- `ui/explained.html` — the public explainer / landing page.
  https://claude.ai/code/artifact/67033b17-b240-411d-a312-8dd2a1c44ba6
- `ui/cerebellum_loop.html`, `ui/line_follower.html` — early single-demo
  pages (superseded, kept for history).
- `cereb/`, `sim/`, `run_*.py`, `test_acceptance.py` — Python V1 library,
  double-pendulum sim, stage experiments, acceptance tests (all pass on V1).
- `FINDINGS.md` — the append-only findings ledger (29 entries). Read fully.
- `tools/analyze_board.py`, `tools/merge_chunks.py` — sweep analysis
  (`py tools/analyze_board.py results/board-YYYY-MM-DD.json`).
- `results/` — committed raw multi-seed sweep JSON (regression baselines).
- `V2-FEATURES.md` — the V2 design doc + functionality-first audit.
- `PORTING.md` — plain-language integration/porting guide.

## BrainV2 (class in testbench.html)

Frozen sparse random basis (FAN=4 inputs/unit, tanh) + linear readout(s).
All constants derive from cfg: {nCtx, nOut, dt, authority} required;
optional {n0, seed, gate, tauElig, leak, rbCap, tauOut, osc, basis, rule,
mix, alpha0, v2:true consumed by the harness}.

Regime switch: `episodic = dt >= 0.1`. It selects:

| mechanism        | continuous (dt < 0.1)        | episodic (dt >= 0.1)      |
|------------------|------------------------------|---------------------------|
| sparsifier       | exact Golgi fixed point in BOTH regimes (finding 30; |
|                  | cfg.sparsifier:'kwta' + cfg.kFrac kept for study)    |
| learning rule    | Autostep (per-weight meta lr)| familiarity-scaled NLMS   |
| fast/slow output | plain sum in BOTH regimes (finding 27; cfg.mix/mixPC)|
| oscillators      | 3 adaptive Hopf channels     | none                      |
| replay ring      | off                          | on (rbCap, masked)        |
| growth           | on (whiteness trigger)       | off (start big via n0)    |

Outcome watchdog (finding 31): optional `brain.outcome(cost)` — host
reports its own non-negative cost metric; pre-engagement cost anchors the
"without the brain" baseline; engaged cost > 1.5x anchor ratchets an
authority cap down (recovers below 1.15x; cap outranks the gate). Wired
in maze + launcher. This is the certifiable harm component.

Always on: projection operator (weights clamped to +-authority), fast-layer
leak (sigma-modification; per-update constant, per-config for shared
brains), slow layer (muS=0.005, no leak), eligibility trace
(td=exp(-dt/tauElig)), pure reads (normalisation adapts only on learn),
NaN guard, competence gate (unless gate:false), error-directed unit
recycling (50% of recycles wired onto stored worst-contexts),
per-(output,unit) energy E for precision merging, novelty counts
(`noveltyBonus(j)` = sqrt(sum s_i^2/(1+cnt_ji))).

Teacher contract: **residual demand** — "what was missing on top of what I
already said". For control: the host controller's own output (FEL). For
decisions: (reward + optional bootstrap) − CURRENT prediction, where the
current prediction comes from a fresh read (finding 19).

Fleet: `mergeFrom(other)` / scenario-side mergePair — precision-weighted
(E) merge; homogeneous fleets merge wf AND ws; all brains must share one
basis seed (finding 22).

## Harness / bench API (browser console)

- `bench.list()`; `bench.run(id, secs, opts)` → {pct, alpha, units, nan,
  stats?, ...} (30 s baseline then `secs` of learning; pct = brain error
  as % of the brainless twin over the metric window). `opts`:
  `{seed}` offsets EVERY RNG (brain basis + scenario streams; 0 = the
  historical deterministic board), `{brain:'v1'|'v2'}` picks the class
  per run, `{ch:{challengeId:value}}` overrides challenge sliders (e.g.
  gossip `{iv:65}` = isolated), `{cfg:{...}}` merges into brainCfg (e.g.
  `{rule:'nlms'}`, `{osc:0}`, `{mix:true}`, `{mixPC:true}`).
- `bench.runBlocks(id, blockSecs, nBlocks, opts)` — samples the metric +
  cumulative scenario stats() after every block (fight/hive/gossip wins).
- `bench.sweep(jobs)` — async job queue ({fn,args,tag} each); progress on
  `window.__SWEEP`; page stays live. `bench.stop()` aborts. Click Pause
  first so the rAF loop doesn't burn CPU between jobs.
- `window.FORCE_BRAIN = 'v1'|'v2'|null` — override the per-scenario class.
- `window.TDMODE = true` — one-step bootstrap in fighter/hive teachers.
- `window.EXPLORE = 'eps'` — force epsilon-greedy (novelty is default).
- Scenario debug handles: `window.__fight/{A,B}`, `__hive`, `__gossip`
  (+ `.meets`). Arc-testing pattern (paste helpers, then call per block —
  keep each browser call under ~25k controlTicks to avoid the 30 s cap):
  fightStart/fightBlock, gosStart/gosBlock as used in the session logs.
- Scenario interface: `SCENARIOS.push({id,name,blurb,dt,brainCfg,metric,
  challenges,init,step(brain,learning)->{ep,eb}|null,draw(g,W,H),
  onClick?})`. Twins doctrine: gray twin = stock algorithm, coral twin =
  stock + brain; byte-identical until the switch flips.

## Current benchmark numbers (V2.1 defaults, 10 seeds, mean +- sd, 240 s)

balance 11.8+-1.8 | path 16.4+-2.8 | car 18.4+-4.5 | flight 11.1+-1.8 |
launcher 38.9+-7.0 | maze 100.5+-1.9 (harm eliminated, finding 31) |
fight winrate/500s-block 68/89/93 | one-mind hive /200s-block 78/93/95 |
gossip-20s 64/91/93 vs isolated 56/87/89.
V1 reference: balance 45.5+-2.1 | path 60.1+-3.6 | car 55.5+-4.3 |
flight 32.5+-1.6 | launcher 42.3+-5.8 | maze 99.4+-2.0 | fight 66/90/90 |
hive 76/92/92. Every continuous scenario beats V1 by 3-5x (finding 30).
Raw data + analyzer under `results/`, `tools/`. CLI board:
`node tools/run_board.js` (~5 min; Node baseline
results/board-v2.1-node.json — 2 ulp tanh offset vs browser, finding 32).

## Known debts

- Python library not at V2 parity.
- fighter/hive/gossip carry three copy-pasted engine IIFEs.
- Headless CLI runner exists (tools/run_board.js, Node 24 installed
  2026-08-26) but is 2 ulp off the browser on Math.tanh (finding 32);
  official numbers stay browser-side until the software-tanh batch.
- GitHub remote: https://github.com/Marzoman5/little-brains (private);
  push after every session.
- Float determinism plan (finding 29): software tanh + Box-Muller
  replacement designed, not implemented — it resets all baselines, so it
  must be its own batch with a fresh multi-seed board.
- Gossip fleet basis drift: `_recycle` re-wires units from each brain's
  own RNG stream, so peer bases silently diverge over long runs, violating
  the shared-basis precondition of finding 22. Unmeasured; investigate
  before long-horizon gossip claims.
