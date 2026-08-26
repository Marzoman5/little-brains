# Cerebellum V2 — feature list and design rationale

Synthesized from the testbench findings ledger (FINDINGS.md) and a three-lens
research panel (learning theory / distributed adaptation / embedded landscape),
2026-08-25. V1 stays frozen as the playground; V2 is built when the experiment
queue below has validated its pieces.

## The novelty verdict (be exact about this, always)

The skeleton (frozen basis + linear readout + LMS + FEL) is CMAC (Albus 1975)
plus Kawato (1987) and has controlled machine tools since the 1990s. The
closest living relative is Pilarski's bionic-limb GVF work (U. Alberta).
Cite both first, then claim ONLY these four islands, where the panel found no
direct precedent:

1. **The competence gate as a ratchet with a pre-engagement harm anchor** —
   "silent until proven, withdraws if you'd be better off without it."
2. **The whiteness growth trigger** — grow capacity only on converged weights
   + autocorrelated residual (RAN allocates on instantaneous error; a
   residual-whiteness stopping rule appears original).
3. **Designed-in fleet fusability** — the shared frozen basis makes weight
   vectors coordinates in one linear space: merging brains is averaging
   solutions of one convex problem. Deep-net federated learning only
   approximates this; here it is exact, and diffusion-LMS theory (Sayed et
   al.) applies verbatim with stability proofs.
4. **The certification decomposition as a product** — learner stays QM;
   only the few-hundred-byte gate/clamp/watchdog is developed at SIL
   (Simplex / run-time-assurance pattern). No commercial MCU equivalent.

## The five limiting factors of V1

1. **The basis is blind** — wired by dice, capacity spread uniformly.
2. **Timescales are hand-set constants** — lr, leak, replay depth; every
   dip we ever debugged traced to one of them being wrong for a stream.
3. **Credit is one-step** — no value bootstrapping; decisions can't see
   setups and combos.
4. **No uncertainty** — the gate is reactive; exploration is blind;
   merging can't weight who actually knows.
5. **Periodic structure is represented badly** — a random tanh basis needs
   many units to tile a phase that two sin/cos channels span exactly.

## V2 features

### Core learning
- **Per-weight adaptive step sizes** (Autostep/TIDBD; Sutton line). Replaces
  lr AND leak AND the familiarity heuristic with learned per-unit rates;
  interference armor for the hive; the step-size vector doubles as a
  consolidation map. Trap: leak must scale with step size or annealed
  weights die silently. (+3 vectors, ~+1 us)
- **Diagonal-Kalman per-weight variance** (K1/K2). Predictive gating
  ("attenuate where evidence is thin"), LinUCB exploration (score +
  c*sqrt(variance) replaces epsilon-greedy), regime-change detection, and
  the precision needed for optimal merging. (+1 vector, ~+0.5 us)
- **True Online TD(lambda) mode** for decision scoring (SwiftTD's
  effective-rate bound keeps the safety contract provable). Control path
  stays FEL. Warning: the gate makes learning slightly off-policy (Baird);
  learn on the acted output. (~+1-2 us)
- **Error-directed unit recycling** (RAN 1991 + generate-and-test).
  Recycled units are re-wired to fire on stored high-error contexts
  (replay ring already holds them); require the bad context to recur
  before targeting; keep directed fraction < 20%. (zero steady-state cost)
- **Benna-Fusi k=3 weight chain** — replaces the hand-tuned fast/slow pair
  with log-spaced coupled timescales; power-law forgetting; removes the
  per-stream leak constant that caused the hive dips. Cheap alternative to
  try first: learned convex mix of fast/slow outputs (Arenas-Garcia 2006,
  provably >= the better layer at all times; an afternoon of work).

### Context (self-wiring)
- **Adaptive oscillator channels** (WFLC / Hopf; Righetti-Ijspeert).
  1-3 residual-driven frequency trackers synthesizing sin/cos channels;
  triggered by the existing residual-autocorrelation test (which currently
  answers "add random units" — for periodic residuals the right answer is
  "add two sinusoid channels" at 1/100 the capacity). Pre-allocate the
  channels at init to keep determinism. (~6 floats, ~0.1 us each)
- **Delay embeddings** as standard optional channels (k past values of key
  inputs) for hosts with no state observables.
- **Context health report** — a diagnostic that says "your context does not
  explain the residual" (finding 15 turned into a feature).

### Fleet ("instinct" sharing) — the users' download/gossip asks
- **Instinct File v1** (~0.6-1.3 KB): header {version, basis seed, fan-in,
  growth-event log, n_ctx, dt-class}, payload {locked mu/var stats, w_slow
  int16 + scales, per-unit energy E, sample count}, footer {lineage version
  vector, parent hash, CRC, optional signature}. Export w_slow ONLY —
  w_fast never travels (finding 13, socialized).
- **Encounter-as-combine gossip** (diffusion NLMS, adapt-then-combine
  ordering — ATC provably beats combine-then-adapt). A radio contact = one
  O(n) convex combination. Solo step-size stability range survives any
  meeting topology (Sayed's theory, verbatim).
- **Precision-weighted merge** (diagonal-Fisher; exact-ish here because the
  problem is linear): units A never exercised don't overwrite B's hard-won
  weights — disjoint experiences compound instead of diluting.
- **Push-sum mass** (one scalar per skill): experience-weighted averaging
  correct under asynchronous, one-way, non-uniform meetings — the parked
  chatty agent cannot outvote the quiet veteran.
- **Trimmed-mean flock merge + replay-validated pairwise merge** —
  tolerates one arbitrary bad agent with zero configuration.
- **Lineage version vectors** — kills the rumor-echo (B's experience counted
  twice via two paths); doubles as audit/rollback.
- **Shadow-gate imports**: any instinct file lands at zero authority and
  buys influence only through locally proven prediction skill. The safety
  contract survives open-world sharing with no new proofs.
- **Growth policy in fleet mode**: coordinated PRNG stream (logged offsets
  in the file header) or growth disabled — divergent growth breaks weight
  comparability, the one thing the whole fleet story rests on.

### Embedded / product
- **Q15/Q31 fixed-point build** (CMSIS arm_lms_norm_q15 is the reference).
  Hazard: the slow layer stalls below 1 LSB in Q15 — needs Q31 +
  stochastic rounding. Target: <2 us/step on a 48 MHz M0+.
- **Persistence policy**: default volatile (relearn-on-boot is a
  certification FEATURE), converged-trigger checkpoints via EEPROM
  emulation, FRAM premium tier. Never naive autosave (flash wear).
- **First wedge product**: continuously-adapting, competence-gated
  anticogging/ripple trim for FOC drives (electrical-angle harmonic
  channels + load/temp context; teacher = the loop's own correction).
  Honest positioning: "continuous, safety-gated AFC generalized to
  nonlinear multi-context residuals" — never "we invented cogging comp"
  (ODrive/Yaskawa/HDD-servo prior art is thick; do an FTO scan).
- **Certification kit**: off=no-op statement, worst-case output bound,
  WCET proof, IEC 61508 argument template. The learner is QM; the gate
  gets the SIL. This is arguably the actual product.
- **Teach-by-demonstration mode**: FEL where the teacher is a human's
  teleoperation — goal acquisition without goal detection.

## Experiment queue (testbench scenarios to build/verify, in order)

1. **Gossip hive**: 6 independent brains, ATC merge at round-end
   "meetings"; benchmark vs shared-brain hive vs solo. Expect most of the
   3x to survive decentralization (theory says it should).
2. **Disjoint-regime compounding**: A learns regime X, B learns Y; merged
   brain must match a both-regime solo. (Tests precision merging.)
3. **Echo inflation**: dense meeting triangle; watch mass/E overconfidence
   with and without version vectors.
4. **Poisoned agent**: one bad brain at k=1 and k=5; trimmed mean +
   replay validation must contain it.
5. **Autostep vs NLMS+familiarity** on car + fighter.
6. **Oscillator channels** on car/flight — does the 2.9 cm floor fall?
7. **Variance-gate + LinUCB** in the fighter, replacing eps-greedy.
8. **TD(lambda) fighter** — does multi-step credit push past 96%?

## Audit: proven old work vs our homebrew (functionality-first review)

Adopt (old work wins):
- Projection operator (robust adaptive control) for provable weight bounds;
  note our leak IS sigma-modification (Ioannou 1983) - use the literature.
- Autostep REPLACES the familiarity hack (proven, tuning-free, same job).
- Convex combination of fast/slow layers (Arenas-Garcia 2006 theorem)
  replaces the hand-tuned sum + leak race - pending one bench test.
- Deterministic k-WTA replaces the Golgi ODE/bisection (k-WTA is the
  standard abstraction of Golgi inhibition anyway; the ODE cost us 3 bugs).

Measure, don't assume:
- Hashed tile-coding basis vs tanh basis at equal memory (integer-only
  basis would win the sub-$1 MCU tier if it ties on accuracy).

Keep (ours wins or classical alternative not worth its machinery):
- FEL default teacher (FxLMS noted as escape hatch only).
- NLMS core (RLS is O(n^2) memory; diagonal approximations already listed).
- Competence gate + pre-engagement watchdog (no classical equivalent).
- Whiteness growth trigger, frozen-basis skeleton (the moat), replay, norm.

Principle: the moat is the packaging (3-liner, contract, fleet, cert kit),
not the math - adopting stronger classical parts strengthens the product.
