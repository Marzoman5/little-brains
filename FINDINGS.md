# Findings ledger — raw material for Cerebellum 2.0

Every entry: what the testbench taught us, which scenario taught it, and what
it implies for the next design. Append-only; synthesis happens later.

1. **Feedback-error learning beats error decorrelation for porting.**
   (car) Training on the host controller's own output gives clean credit in
   the module's own units — the car converged with the naive setup where
   error-teaching needed a hand-crafted Stanley signal.
2. **A competence gate must ratchet, and harm must be anchored to the
   pre-engagement baseline.** (pendulum) Correlation collapses on success;
   "personal best" watchdogs false-alarm at the floor. The only stable harm
   definition is "worse than without the brain."
3. **Gates cannot see skill through sparse noisy rewards.** (fighter)
   Reward-scorer configs opt out (`gate:false`); zero-init weights are the
   natural ramp.
4. **All timescale constants must be rate-invariant per stream, not per
   second and not per brain.** (launcher: leak defined in seconds destroyed
   episodic memory; hive: per-update leak at 6x stream rate gave a 3 s
   memory half-life.) leak, replay depth, normalisation rate: per-stream
   quantities. `leak`, `rbCap`, `tauElig` are now config.
5. **The sparsity feedback loop is unstable at episodic rates.** (launcher,
   maze) Solve the Golgi fixed point exactly (bisection) when dt >= tau.
6. **Reads must be pure.** (maze) Normalisation updating on read calls
   thrashed the basis at high read rates. Stats adapt only while learning.
7. **The teacher contract is residual demand** — "what was missing on top of
   what I already said" — never an absolute label. (maze saturation bug.)
8. **Replay must rehearse only taught channels toward stored totals.**
   (fighter "unlearning") Rehearsing untaught channels toward snapshots is a
   built-in forgetting mechanism.
9. **Discrete action scoring wants no eligibility trace; continuous control
   wants ~0.3 s.** (fighter tauElig 0.02) Credit smearing across decisions
   mis-teaches neighbouring choices.
10. **Score-based action selection needs exploration, and exploration should
    be performance-gated.** (fighter) Explore while losing, exploit while
    winning; fixed epsilon is a permanent tax.
11. **Sticky argmax kills estimate-noise dithering.** (fighter) Switching
    actions should require beating the incumbent by a margin.
12. **Dense shaping on the exact skill beats sparse outcomes.** (fighter)
    +blocked-a-hit / -got-blocked taught blocking in one block of rounds
    where damage-only reward needed five.
13. **Naive stacking fails; the lower brain must stabilise first.** (fighter
    predictor) A learner's outputs are a moving distribution; feeding them
    to a second learner destabilises it. Train, then freeze, then feed.
14. **Shared-brain swarms are the strongest multiplier.** (hive) Six
    decorrelated experience streams into one memory: domination ~3x faster
    than solo, and new members are born knowing everything.
15. **The brain can only learn what its context can see.** (maze) When the
    answer is not a function of the observables, it learns nothing, safely.
    Context design is where domain knowledge lives.
16. **Capacity is non-monotonic; growth needs dense samples.** (stage 6,
    launcher) n=100 beat n=1000 on control tasks; the whiteness trigger is
    unreliable under ~1000 samples, so episodic configs start big instead
    of growing.
