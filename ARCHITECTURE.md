# Architecture reference — current state (V2, 2026-08-25)

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
- `FINDINGS.md` — the append-only findings ledger (23 entries). Read fully.
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
| sparsifier       | deterministic k-WTA (8% of n)| exact Golgi fixed point   |
| learning rule    | Autostep (per-weight meta lr)| familiarity-scaled NLMS   |
| fast/slow output | learned convex mix (lam)     | plain sum (mix off)       |
| oscillators      | 3 adaptive Hopf channels     | none                      |
| replay ring      | off                          | on (rbCap, masked)        |
| growth           | on (whiteness trigger)       | off (start big via n0)    |

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

- `bench.list()`; `bench.run(id, secs)` → {pct, alpha, units, nan, ...}
  (30 s baseline then `secs` of learning; pct = brain error as % of the
  brainless twin over the metric window).
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

## Current benchmark numbers (V2 defaults, single seed — see concerns)

balance 35.0 | path 50.4 | car 48.3 | flight 20.5 | launcher 54.4 |
maze ~100 (boundary exhibit) | fighter 90%+ from first 500 s block |
one-mind hive 75/99/93 | gossip-20s 68/89/99 vs isolated 60/77/88
(per-100s or per-200s win blocks; all measured in session, all committed).

## Known debts

- Python library not at V2 parity.
- fighter/hive/gossip carry three copy-pasted engine IIFEs.
- No automated full-suite CI runner; results live in FINDINGS.md prose.
- Single-seed determinism only; no variance bars anywhere.
- `Math.tanh`/float reproducibility across JS engines unverified (matters
  for instinct files and cross-device merging).
