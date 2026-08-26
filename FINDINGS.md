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

## V2 build results (2026-08-25) - all vs V1 on identical tasks/seeds

Shipped in BrainV2 (testbench default): k-WTA sparsity (continuous) /
exact-Golgi (episodic), projection operator, Autostep (continuous) /
familiarity-NLMS (episodic), convex fast-slow mix (continuous, neutral
boot), 3 adaptive Hopf oscillator channels (continuous), error-directed
recycling, per-weight energy for merging, novelty-bonus exploration.

Scoreboard (brain error as % of plain twin; lower is better):
  balance 43.9 -> 35.0 | path 58.1 -> 50.4 | car 48.0 -> 48.3 (tie)
  flight 36.2 -> 20.5 | launcher 77.8 -> 54.4 | maze ~100 (boundary, unchanged)
  fighter first-block 66% -> 90%+ sustained (novelty exploration)
  one-mind hive 60/90/91 -> 75/99/93 (200 s blocks)

17. **Autostep is data-hungry: rule must match stream density.** Meta
    step-sizes win on dense continuous streams, lose badly on ~190-sample
    episodic tasks; NLMS+familiarity stays the episodic rule.
18. **Sparsifier must match regime too**: k-WTA's threshold shaving loses
    discrimination on tiny episodic contexts; exact-Golgi stays there.
19. **Teach with delta = target − CURRENT prediction, choose with
    post-teach scores.** Stale remembered scores in either role cost the
    fighter ~25 winrate points. Fresh reads are ~1 us; correctness is free.
20. **Novelty-bonus exploration beats epsilon-greedy** (90% vs 66% first
    block): optimism toward untaught basis regions explores exactly the
    right actions instead of random ones.
21. **One-step TD bootstrap did not help** the fighter (slower start, same
    ceiling). Multi-step credit remains open; not worth its risk today.
22. **Gossip works and matches the one-mind hive**: isolated 60/77/88 vs
    gossip-20s 68/89/99 vs one-mind 75/99/93. Decentralization keeps the
    fleet benefit, as diffusion-LMS theory predicts. Preconditions learned
    the hard way: all brains must share ONE basis seed, and homogeneous
    fleets must merge BOTH layers (slow-only transfers ~nothing at demo
    timescales; the fast-never-travels doctrine is for heterogeneous
    fleets).
23. **Tile coding loses at equal memory** (balance 72.6 vs 35.0; car 85.6
    vs 48.3, untuned tile hyperparameters noted): the tanh random basis
    survives its CMAC challenge for these smooth-dynamics tasks.

## The variance audit (2026-08-25) — every RNG seeded, 10 seeds, error bars

Instrument: `bench.run(id,secs,{seed,brain,ch,cfg})` threads one seed
offset through the brain basis AND every scenario RNG; `runBlocks` samples
cumulative stats per block; `sweep()` runs job queues in-page. Seed 0
reproduces the historical board bit-exactly. Raw data: `results/`.

24. **Single-seed numbers lied in both directions; V1 is low-variance,
    V2 (as shipped) high-variance everywhere.** 10-seed re-scores of the
    documented headline claims (pct, 240 s, mean +- sd):
    car "tie" was actually V2's clearest win (V1 55.5+-4.3 -> V2 41.0+-8.5,
    9/10 seeds); path "58->50 win" was actually a V2 loss (60.1+-3.6 ->
    66.4+-20.7, 5/10); flight "36->20 win" was not reproduced (32.5+-1.6 ->
    35.2+-8.9, V2 wins 3/10); launcher's 23-point gap is ~3 points, inside
    noise — and the launcher metric window is so short that one seed's pct
    swings 43->81 with duration alone (record durations with all claims);
    fighter's "90% from the first block" (finding 20's 90-vs-66) dissolves:
    first-block winrate V2 68+-21 vs V1 66+-8 — the novelty-exploration
    advantage was seed luck, though 90%+ SUSTAINED holds (blocks 2-3);
    hive: V1 ties V2 (77/92/92 vs 78/93/95); gossip>isolated reproduces
    cleanly (overall 82.5+-1.5 vs 76.9+-3.4) but gossip does NOT fully
    match the one-mind hive (88.3+-1.9) — finding 22 overstated it.
25. **The harm watchdog can fail in decision mode.** (maze, V2) Seed 1:
    brain twin 643.7% of plain (6.4x MORE wasted steps) with alpha=1 the
    whole way; V1 never exceeds 101.4 on any seed. Mechanism: the gate's
    harm anchor is teacher power, and the maze teacher is a sparse
    end-of-episode residual label — it never reflects the twin's actual
    performance, so "worse than without it" is invisible to the watchdog.
    Also: after a veto diverts the walker, twin paths stay diverged even at
    alpha=0, so the metric can't recover. The certification story leans on
    this watchdog; a decision-mode harm signal (e.g. outcome-anchored, not
    teacher-anchored) is now a required frontier, not an option.
26. **Paper-exact Autostep LOSES to our misremembered version, and the
    reason is a doctrine.** Line-by-line audit vs Mahmood/Sutton/Degris/
    Pilarski 2012 (paper PDF in hand): everything matches except eq. (7) —
    the paper STORES the alpha/M normalization; we apply it transiently.
    Stored-alpha betas stay differentiated (the paper's point) but total
    adaptation is throttled; tested on the board it lost every continuous
    scenario (balance 38->56, path 66->84, car 41->58, flight 35->50,
    10-seed means). The transient form pins the effective step at the NLMS
    stability ceiling — maximum-rate tracking — which is what nonstationary
    FEL control wants. Deviation now documented in code as deliberate.
    Corollary: saturated Autostep ~ unit-step trace-NLMS; ablation shows
    its real edge over familiarity-NLMS is mostly the car (48.2 -> 41.0);
    balance/path/flight are ties.
27. **The learned convex fast/slow mix is dead: plain sum wins.** The
    recursion matched Arenas-Garcia 2006 almost verbatim (even our "board
    vote" neutral boot IS the published init), but the paper adapts each
    filter on ITS OWN error while our layers share one teacher. Both forms
    measured, 10 seeds x 4 continuous scenarios (board pct sum): shared-
    teacher mix 181, per-component mix (mixPC, paper structure) 179, mix
    OFF 167. The two-rate plain sum (Smith 2006 — V1's design) beats the
    adaptive-filtering combination in this architecture. Default is now
    mix:false; machinery kept behind cfg.mix/cfg.mixPC.
28. **Oscillators carry flight and car, hurt balance.** Removing them
    (osc:0): flight 35.2->47.8 (+12.6, worse on 9/10 seeds), car
    41.0->50.5, balance 38.3->32.2 (they HURT balance), path tie. Net
    board value positive — kept. What they lock onto and why they inject
    noise into balance remains unstudied (concern #6 still open).
29. **Bit-identical floats across devices are dead on native Math.** As of
    Chrome >=148 (V8 c1486295ae5) Math.tanh calls the HOST libm — different
    bits on Windows/macOS/Linux in the same browser version; Safari always
    did; Firefox's cos differs per-OS by pref default. Only + - * / sqrt
    fround imul are spec-exact. Instinct files / cross-device merges need
    a software tanh (clamped [7/6] rational or Cephes port) and a Box-
    Muller replacement (or software log/cos), plus a mathProfile stamp in
    the file header. Design decision recorded; not yet implemented (it
    changes the basis, so it resets all baselines — do it as its own
    batch).

Adopted defaults after this batch (V2, mix off): balance 33.0+-11.7 |
path 59.1+-13.2 | car 39.2+-6.9 | flight 35.7+-9.4 | launcher-240
38.9+-7.0 | maze: harm outliers (finding 25) | fight blocks 68/89/93 |
hive 78/93/95 | gossip-20s 64/91/93. V2 now beats V1 on balance+car,
ties path/launcher/fight/hive, loses flight slightly (32.5+-1.6 V1),
and maze needs the finding-25 fix before any "harmless" claim.
