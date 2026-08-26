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

## The V2.1 batch (2026-08-26) — the sparsifier was the bottleneck

30. **The exact Golgi fixed point is the right sparsifier in BOTH
    regimes — the k-WTA continuous default was costing 2-4x.** Nobody
    had ever tried the per-step exact solve in continuous mode: V1 used
    a lagging Golgi ODE there, V2 replaced it with deterministic k-WTA
    (8%). Head-to-head, 10 seeds, 240 s (pct, lower better):
    kwta -> golgi: balance 33.0->11.8+-1.8, path 59.1->16.4+-2.8,
    car 39.2->18.4+-4.5, flight 35.7->11.1+-1.8 — and variance collapses.
    Mechanism (controlled): ~2/3 is code density (kwta at matched
    kFrac=0.184 recovers to 16.6/37.5/28.0/15.1) and the rest is the
    adaptive, shave-free threshold (matched-density kwta still loses
    everywhere). Golgi needs NO density constant, barely grows (64-130
    units vs 130-320 — beats bigger brains with less capacity), stays
    healthy at 900 s (flight 3.9), and DELETES one regime switch plus
    the kFrac constant. V2.1 now beats V1 3-5x on every continuous
    scenario. Corollary: finding 16's "capacity is non-monotonic" was
    partly the sparsifier's fault — with the right code, 64 units
    suffice where k-WTA needed 300.
    Also tested: Autostep's e*s effective-step bound (the tight
    no-overshoot bound; 2012-audit D2). Spectacular on three scenarios
    (balance 5.1, flight 8.2+-0.2) but blows up on path (49.9+-38.1) —
    fails the whole-board rule, rejected: the e^2 conservatism is
    load-bearing on the highest-gain task.
31. **The outcome watchdog closes finding 25 — after teaching its own
    lesson about anchors.** New optional wire: `brain.outcome(c)` — the
    host reports the non-negative cost it already measures (wasted
    steps, |miss|); cost while the gate is closed anchors the
    pre-engagement baseline; engaged cost above 1.5x anchor ratchets an
    authority cap down 0.7x (recovers slowly below 1.15x). The cap
    outranks the correlation gate. First (naive) version anchored on
    1-2 samples and STRANGLED the healthy launcher (43.2 -> 94.8) —
    finding 2's false-alarm lesson, relearned on the other side: the
    anchor needs >=8 samples, a slow (0.1) cost EMA, >=4 engaged
    samples before judgment, and hysteresis. Robust version: maze
    contained on all 10 seeds (100.5+-1.9, max 104.6 vs yesterday's
    643.7) while the launcher is bit-identical (43.2). Wired into maze
    + launcher; fight/hive/gossip left unwired (bounded cost + gate:off
    makes it inert there). The certification story now has its explicit,
    auditable "worse than without it" component. Residual truth: maze
    settles AT ~100 — the veto brain is neutralized, not helpful; the
    boundary exhibit is honest again.
32. **Same-machine engines already disagree on tanh — measured.**
    Chrome 148 (host UCRT libm) vs Node 24 (V8 13.6 fdlibm port):
    tanh(0.8) differs by 2 ulp; after 240 s the car drifts 16.2 vs 16.5
    pct. exp(1) agrees. Finding 29 is no longer theoretical: the new
    headless CLI runner (tools/run_board.js — full 10-seed board in
    ~5 min, one command) is numerically honest but NOT bit-compatible
    with the browser rig until the software-tanh batch lands. Official
    numbers stay browser-side; the Node baseline
    (results/board-v2.1-node.json) is the CI diff reference.

V2.1 board (browser rig, 10 seeds, mean +- sd): balance 11.8+-1.8 |
path 16.4+-2.8 | car 18.4+-4.5 | flight 11.1+-1.8 | launcher 38.9+-7.0 |
maze 100.5+-1.9 (harm eliminated) | fight 68/89/93 | hive 78/93/95 |
gossip-20s 64/91/93. Every continuous scenario beats V1 3-5x; nothing
regressed; two mechanisms and one constant removed.

## Closing the cerebellum book (2026-08-26 evening) — the limit probes

33. **The interference catastrophe did not happen: proprioception breaks
    regime aliasing.** The rover (frozen blind scenario, concern #2 —
    predictions committed before the first run at e6633bc) alternates
    three terrains with sign-opposed steering faults. Frozen prediction:
    blind mode (no slip/drift channels) averages destructively,
    ~85-115%. MEASURED: blind 15.3+-7.4 vs slip-sensed 12.5+-6.8 (10
    seeds, 240 s) — nearly identical; still 17.3+-7.7 at 10 s dwell;
    no degradation at 600 s; V1 does 29.8. Mechanism of the wrong
    prediction: the "blind" context kept yawRate, and commanded-vs-
    achieved motion IS a slip sensor — FAN=4 random conjunctions of
    (command, response) channels linearly separate three terrains
    without any dedicated regime sensor. Finding 15 sharpened: the
    averaging catastrophe requires regimes invisible to EVERY channel
    including proprioceptive discrepancies — rarer in real plants than
    theory feared, because acting on a plant reveals it. MOSAIC-style
    multiple models stay on the menu but now need a genuinely aliased
    case (e.g., opponent intent before first contact) to earn their way
    in. A truly-aliased rover variant (yawRate removed) remains untested.
34. **No bursting within a million steps.** F2 (drift-then-burst under
    FEL's self-quenching excitation) probed: balance, 10,000 s
    (10^6 steps at dt 0.01), 3 seeds, sensor noise 0 and 0.01 rad,
    health telemetry per 500 s block. Result: zero NaN, zero weights at
    the projection clamp at any sample, fast-layer norms DECAY over the
    run (27->8; sigma-mod doing its job), pct stays in a stable band
    (2-13 clean / 7-17 noisy) with no burst excursion in 120 sampled
    blocks. The risk is bounded, not eliminated: one scenario, 10^6
    steps, moderate noise. The health() telemetry (clamp fraction, norm
    drift) is now permanent bench equipment for longer bounds.
35. **Compute audit (what it actually costs).** Measured (Node x64
    desktop, capacity pinned): control brain n=64/nCtx=8/J=1 learning
    12.8 us/step (9.4 with the NLMS-only build), reads 6.1 us; n=128/J=2
    42 us; 320-unit/8-output decision reads 169 us. Live float64 memory
    n=64/J=1: 7.9 KB (6.4 KB NLMS build — beta/h/v are 1.5 KB). Derived
    MCU estimates (Q15 + tanh LUT + warm-started bisection): Cortex-M4
    ~60-100 us/step (1% CPU at 100 Hz); Cortex-M0+ ~200-260 us (2-3%
    at 100 Hz); ATmega328 (Arduino Uno) ~1-1.7 ms/step -> 50-100 Hz
    loops at 7-17% CPU with n=32-64 in ~0.8-1.5 KB RAM; PIC18 similar,
    dsPIC33/PIC24 M4-class. Smallest sensible target: ATmega328/PIC18,
    n=32, 50 Hz, <1 KB RAM. Cheapest speedups not yet taken: warm-start
    the Golgi bisection from the previous threshold (24 iters -> ~4, the
    single largest cost after V2.1), active-set-only loops (~20% fire),
    Q15 packing, NLMS-only build, fixed capacity (kills the 1024-unit
    preallocation). See docs/APPLICATIONS.md for the deployment map.

## The library batch (2026-08-26 evening) — Tier 1 of the roadmap

36. **The standalone library exists, and its C port is bit-identical to
    JS.** `lib/littlebrains.js` (mathProfile "lb2"): every transcendental
    replaced with software implementations built from the IEEE-754
    spec-exact operation set (rational tanh, 2^k-split exp, wrapped-phase
    sin/cos, atanh-series log, Irwin-Hall-12 gaussian for the basis) —
    plus honored capacity (n=16 allocates 16: ~2 KB), cfg-only seeding
    (no window globals), and persistence as CALLER-OWNED BYTES:
    `save()` returns a versioned CRC-checked LB02 blob, `load()`
    restores it; the library never touches storage (browser saves the
    blob wherever it wants, a PIC writes EEPROM — same bytes). Blobs
    refuse wrong basis seeds ("weights are coordinates of the basis")
    and corrupt CRCs. `lib/c/lb.c` (C99, arena-allocated, no malloc, no
    libm-transcendentals, -ffp-contract=off) reproduces the JS reference
    BIT-FOR-BIT through three golden-vector runs on the FIRST compile:
    4000 closed-FEL-loop steps with Autostep+oscillators+Golgi+gate —
    including an identical 16->20 growth event — plus episodic
    NLMS+replay+outcome and kwta configs (tools/gen_vectors.js,
    lib/c/test_vectors.c). Smoke suite: 12/12 (tools/test_lib.js),
    including a 16-unit brain opening its gate in a real closed loop and
    cutting tracking error to 23% of the plain controller. Determinism
    is now a THEOREM about conforming implementations, not an empirical
    bet — this closes findings 29/32's plan at the library level.
    NOT yet done: testbench still runs its own old-math BrainV2 — the
    swap + full lb2 board re-baseline is the next session's opener.
37. **The PIC16-class integer build taught four fixed-point lessons and
    is not done.** lb16 (Q15/Q12/Q27+Q10, ~500 B RAM, LUT tanh, no
    division in per-weight loops, square-compare gate with no sqrt,
    block-floating-point AGC normalizer with no division): all
    mechanisms run, but the closed FEL loop plateaus at rms 0.24-0.40 vs
    the float reference's 0.076, and the gate bootstrap deadlocks.
    Lessons, each found the hard way: (a) int16 eligibility saturates
    and destroys credit — the trace needs its own Q-scale derived from
    its ~15-step accumulation, not the activation's; (b) fixed-point
    EMAs stall below their own LSB (the amp tracker sat at 1 forever) —
    keep small EMAs in x16 precision; (c) asymmetric >> rounding drifts
    means — round-half-up; (d) a closed gate turns FEL's DC teacher
    into open-loop weight integration to the projection clamp
    (gate-closed runaway) — float escapes because its gate opens before
    runaway; the integer gate's noisier correlation doesn't. ALSO: the
    consistent-rescaling experiments proved the integer pipeline is
    exactly scale-invariant (three different unit conventions, identical
    real behavior) — the arithmetic is sound; the remaining gap is
    statistical quality, not scales. Finish method (do NOT keep tuning
    blind): stage-wise golden vectors from lb.c — basis codes, then
    eligibility, then NLMS deltas, then gate rho, verified one stage at
    a time. M0+-and-up targets are served TODAY by lb.c.
