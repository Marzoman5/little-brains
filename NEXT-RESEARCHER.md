# You are the next researcher on the Little Brains project

You are a Claude Code session inheriting a working research project. Your
mandate is not maintenance — it is to understand this project more deeply
than its builders, attack it, and take it past them. You have full autonomy
to research, run experiments, modify code, and ship V2.1 — or V3, if and
only if your findings genuinely justify the number.

## First actions (do these before anything else)

1. Read, in order: `FINDINGS.md` (the 23-entry ledger — this is the
   project's accumulated wisdom, every entry was paid for), `V2-FEATURES.md`
   (design doc + the functionality-first audit), `ARCHITECTURE.md` (exact
   current state, bench API, known debts), `PORTING.md`.
2. Open `ui/testbench.html` in the browser, run `bench.list()` and one
   `bench.run('balance', 240)` to confirm the rig works for you.
3. Git discipline (non-negotiable): the repo has local commits documenting
   every finding. `git log --oneline` to see the history. Commit BEFORE and
   AFTER every experiment batch, with findings in the commit message.
   There is no GitHub remote yet and no `gh` CLI installed — in your first
   session, ask the user to help you set up a private GitHub repo and push
   the full history as backup, then push after every working session.
4. Reproduce two baseline numbers yourself before changing anything
   (e.g. balance ~35 pct, launcher ~54 pct). If you cannot reproduce, stop
   and diagnose — do not build on a rig you haven't verified.

## The premise (what this project IS)

A drop-in learning sidecar for loops and decision points — "your
controller, plus a cerebellum, in three lines":

    u  = controller.step(err)
    u += brain.step(ctx, u)     // FEL: the teacher is the reflex itself

Frozen sparse random basis, trainable linear readout, no backprop, no
gradients stored, microseconds per step, kilobytes of RAM, deterministic.
The MATH is old and we say so loudly: CMAC (Albus 1975) + LMS (1960) +
feedback-error learning (Kawato 1987) + two-rate memory (Smith 2006).
The novelty lives in exactly four places — defend only these:
  (1) the competence gate: silent-until-proven, with a harm watchdog
      anchored to pre-engagement performance ("worse than without it" is
      the only harm definition wired in);
  (2) the whiteness growth trigger (grow only on converged weights AND
      autocorrelated residual);
  (3) designed-in fleet fusability: one shared frozen basis makes weight
      vectors coordinates of one convex problem — merging brains is exact,
      not a deep-net heuristic, and diffusion-LMS theory applies verbatim;
  (4) the certification decomposition: the learner stays uncertified, only
      the few-hundred-byte gate/clamp/watchdog would carry a SIL rating.
The MOAT is the packaging: the 3-line integration, the safety contract,
the fleet story, the testbench-as-immune-system. Improving internals with
better classical parts strengthens the product; it never dilutes it.

## The scientific protocol (how we work — hold yourself to it)

- The bench is the judge. Every change must beat or tie the WHOLE board
  (all nine scenarios), not just its target scenario. Losers get reported
  in FINDINGS.md anyway. No tuning a constant against a test after seeing
  that test's result — constants are chosen once, board-wide.
- No rigging, no cherry-picking seeds, no "best case" demos. When a result
  is noise-limited, say so and measure variance instead of arguing.
- When something fails, diagnose to the mechanism before fixing. The most
  valuable entries in FINDINGS.md are the failures (13, 17, 18, 19, 21).
- Honesty about novelty is identity: cite CMAC/Kawato/Pilarski FIRST.

## The audit I owe you: my concerns (attack these first)

> STATUS 2026-08-25 (session 2): #1 RESOLVED (multi-seed bench + error
> bars; findings 24, results/), #4 RESOLVED (Autostep audited vs paper,
> paper-exact form tested and lost; finding 26), #5 RESOLVED (exact
> recursion + per-component variant tested; the mix is now OFF by
> default; finding 27), #8 RESOLVED as a design (finding 29 — native
> Math.tanh is now OS-dependent in Chrome; implementation deferred),
> #12 PARTIAL (in-page sweep runner + committed JSON baselines; a CLI
> runner is blocked on installing Node). #6 sharpened by finding 28
> (oscillators hurt balance — why?). #7 sharpened into finding 25: the
> harm watchdog demonstrably FAILS in decision mode (maze seed 1) — this
> is now the highest-priority open problem, since certification is the
> product. #2 (blind scenario), #3, #9, #10, #11 remain untouched.

1. **Everything is single-seed.** Every number in FINDINGS.md is one
   deterministic run. Small deltas (car 48.0 vs 48.3) are meaningless and
   nobody knows the variance of the big ones. Build multi-seed support
   into the bench (seed parameter through scenario RNGs + brain seed) and
   put error bars on the headline claims before trusting anything else.
2. **The bench and the brain co-evolved.** Nine scenarios were built and
   debugged alongside the learner — classic overfitting risk. Build at
   least one genuinely NEW scenario (blind: design it, freeze it, THEN run
   V2 unmodified). If V2 underperforms there, that finding outranks
   everything else.
3. **The "regime-aware" switches smell.** episodic-vs-continuous now flips
   the sparsifier, the learning rule, the mixer, replay, growth, and
   oscillators — six mechanisms keyed on one threshold (dt >= 0.1). Each
   flip was empirically justified, but ask: is there a single principled
   mechanism that handles both regimes, or is this bench-fit disguised as
   doctrine? (E.g., would Autostep with a better init/prior win episodic
   too? Is the k-WTA discrimination loss fixable by using soft-threshold
   at the k-th value with a floor?)
4. **My Autostep is from memory.** Verify it line-by-line against Mahmood,
   Sutton, Degris, Pilarski 2012 (Autostep) — the normalizer update and
   the effective-step bound especially. An implementation bug here would
   silently cap the continuous-regime results.
5. **The convex mix is my crude gradient**, not the exact Arenas-Garcia
   2006 recursion, and it showed boot-value sensitivity across scenarios
   (finding: neutral boot chosen by board vote). Implement the paper's
   exact update + their transfer-of-coefficients trick and re-vote.
6. **Oscillators are under-studied.** K=1.5, count=3, initial frequencies
   0.3/0.9/2.7 Hz, driven by the MEAN teacher across outputs (a hack for
   nOut>1). They produced the flight win — find out what they actually
   locked to, whether they help car/balance at all, and whether per-output
   drive is better. Consider WFLC/BMFLC as the alternative tracker.
7. **The gate is barely exercised in V2's wins** (mostly gate:false or
   alpha=1 by the measurement window). Its ramp/watchdog interactions with
   Autostep and the mixer are untested. Design a scenario where the gate
   MUST save the day (adversarial context, wrong plant) and verify.
8. **JS float determinism is assumed, not proven.** Math.tanh and FP
   ordering may differ across engines; instinct files and cross-device
   merging depend on bit-comparable bases. Verify or make the basis
   integer/LUT-based.
9. **Python is stale** (V1 + partial back-ports). Either back-port V2 and
   make `test_acceptance.py` cover it, or formally demote Python to
   "historical reference" in the docs. Don't leave it ambiguous.
10. **Three copy-pasted fight engines** (fighter/hive/gossip IIFEs). Any
    balance change now needs three edits. Unify behind one shared factory
    when you next touch them.
11. **The tile-coding verdict used untuned tiles** (finding 23 says so).
    If you have reason to think a tuned hashed-tile config could win at
    equal memory on an MCU-integer budget, run the fair fight.
12. **No CI.** Add an automated full-suite runner (headless, one command,
    JSON baselines committed to the repo) so regressions are caught by
    diff, not by vibes.

## Study curriculum (read before theorizing; verify claims against these)

- Sutton line: IDBD (1992), Autostep (2012), TIDBD (2018), SwiftTD (2024,
  the effective-learning-rate bound), true online TD(lambda) (2014),
  Sutton & Barto ch. 9-12 (tile coding, on/off-policy traps, Baird).
- Adaptive filtering: NLMS stability; Arenas-Garcia 2006/2016
  (combinations of adaptive filters); FxLMS (filtered-x, the classical
  answer to plant-lag credit); Sayed, "Adaptation, Learning, and
  Optimization over Networks" (diffusion LMS — the gossip theory).
- Cerebellar: Marr 1969 / Albus 1971 / Fujita 1982; Kawato FEL 1987;
  Porrill & Dean decorrelation; Smith et al. 2006 two-rate; Benna-Fusi
  2016 (the k-timescale chain we haven't built).
- Capacity/allocation: Platt RAN 1991; Mahmood & Sutton generate-and-test
  2013; Dohare et al. continual backprop, Nature 2024.
- Uncertainty: Sutton K1/K2 1992 (diagonal Kalman gains); LinUCB 2010.
- Robust adaptive control: Ioannou sigma-modification; projection
  operators; Simplex / run-time assurance (the certification story).
- Distributed: push-sum (Kempe 2003); covariance intersection (Julier &
  Uhlmann 1997); Byzantine-robust aggregation (Yin et al. 2018); CRDTs.
- Oscillators: Righetti-Buchli-Ijspeert 2006; WFLC (Riviere-Thakor).
- Embedded: CMSIS-DSP arm_lms_norm_q15; ST NanoEdge (the landscape
  incumbent); Pilarski's bionic-limb GVF program (our closest relative —
  diff every novelty claim against it).

## Open frontiers (a menu, not an order — follow your own findings)

- **Multi-step credit done right**: one-step TD bootstrap failed (finding
  21). True-online TD(lambda) with SwiftTD's rate bound, or n-step
  returns, on the fighter — mind the gate's off-policy trap (Baird).
- **Heterogeneous gossip**: different plants, slow-layer-only,
  similarity-gated merges (compare mu/var vectors). Needs a scenario where
  swarm members face DIFFERENT faults. This tests findings 22's boundary.
- **Instinct files**: the export/import format speced in V2-FEATURES.md
  (seed + growth log + locked stats + w_slow + lineage). Build it + a
  poisoned-import test behind the shadow gate.
- **The predictor stack, stabilized**: train the opponent-model to
  convergence, FREEZE it, then feed its outputs as context (naive live
  stacking measurably failed — finding 13). This is the layered-brains
  road.
- **Uncertainty substrate**: K1-style per-weight variance powering a
  predictive gate, principled LinUCB exploration, and merge precisions —
  one mechanism, three consumers (V2-FEATURES.md idea 3).
- **Benna-Fusi k=3 chain** replacing leak entirely (removes the last
  per-config retention constant).
- **The C header** (`brain.h`) + fixed-point port + one physical build
  (ESP32 line-follower with a crooked wheel — the best 30 seconds of
  video this project could produce).
- **New testbench domains** — strongly encouraged. The one I most wish
  existed: an **ops-loop scenario** (simulated server fleet: diurnal +
  weekly load with noise; a reactive autoscaler as the host controller;
  brain learns feedforward scaling/prewarming from phase channels). It
  would demonstrate the concept to the largest audience software has, and
  periodic-load prediction is exactly this architecture's food. Other
  candidates we never built: quadcopter hover under gusts (visually
  spectacular), HVAC/thermal mass, prefetcher/cache-policy scoring
  (episodic decision mode), adaptive game difficulty. Also think past
  these: anywhere with a loop, a repeatable pattern, and an observable
  context is candidate territory — find the ones we didn't imagine.

## Versioning rules

- V2.1: refinements, fixes, new scenarios, ports — the architecture
  recognizably the same.
- V3: only if you change something foundational (basis, learning
  substrate, memory architecture) AND it beats the full board with
  variance bars AND you can articulate why in one FINDINGS.md entry.
  The number must be earned; resist inflation.

## Deliverables per working session

1. Findings appended to FINDINGS.md (failures included, mechanisms named).
2. Bench green (full board) or regressions explicitly justified.
3. Git commits with findings in messages; GitHub push once remote exists.
4. The published artifacts updated (same URLs — republish, don't fork).
5. A plain-language summary for the project owner: what changed, what was
   measured, what died, what's next.

## Final calibration

You inherit working, measured, honest machinery — respect it by attacking
it. Nothing here is sacred: if V1's Golgi ODE, the familiarity hack, or
the whole two-rate memory turn out to beat their V2 replacements under
fair multi-seed tests, restore them and write the finding. The previous
researcher was wrong repeatedly and the ledger's best entries came from
those moments. Your job is to be wrong faster, about deeper things, with
better instruments — and to leave the next researcher a sharper mandate
than this one.
