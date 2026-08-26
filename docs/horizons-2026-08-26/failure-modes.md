All key citations verified. Now composing the report.

# Adversarial review: hidden logic faults in the Little Brains class of system

Scope: adaptive linear-in-frozen-features sidecar (sparse random tanh basis, two-rate linear readout, NLMS/Autostep), FEL teacher, competence gate + outcome watchdog, projection clamp, fleet merging via precision-weighted averaging. Cross-referenced against the actual V2.1 mechanism table (ARCHITECTURE.md): fast-layer leak = σ-modification (fast only; **slow layer has no leak**), projection = ±authority clamp, gate ramp, outcome watchdog ratchet (engage >1.5× anchor, recover <1.15×), episodic replay ring, whiteness growth trigger, gossip merge with the known basis-drift debt.

Executive framing: this design already carries three of the four classical robustness modifications (σ-mod on fast weights, projection, and the gate as a coarse engagement-level dead zone). The faults below are what those do NOT cover — and most of them concentrate on the **slow, unleaked consolidation layer**, the **gate's teacher-mimicry criterion**, and **timescales longer than the 240 s bench window**.

---

## F1. Rohrs-type instability: adaptation through unmodeled dynamics

**Literature.** Rohrs, Valavani, Athans & Stein, "Robustness of continuous-time adaptive control algorithms in the presence of unmodeled dynamics," IEEE TAC 30(9), 1985: MRAC schemes with global stability proofs are destabilized by parasitic high-frequency dynamics plus either (a) a small sinusoidal reference at a phase-critical frequency or (b) broadband output noise. Fixes: σ-modification (Ioannou & Kokotović, Automatica 20(5), 1984), e-modification (Narendra & Annaswamy, IEEE TAC 32(2), 1987), dead zones (Peterson & Narendra, IEEE TAC 27(6), 1982; Kreisselmeier & Anderson, IEEE TAC 31(2), 1986), projection (Ioannou & Sun, *Robust Adaptive Control*, 1996).

**Trigger here.** Actuator lag, filter dynamics, or discretization phase between the brain's output injection point and where the teacher's error is measured — i.e., any plant the bench didn't model. A small periodic disturbance near the parasitic phase-crossover frequency (Rohrs' scenario 2 is exactly a crosswind or engine-vibration line).

**Mechanism.** The gradient direction φ·e assumed by LMS/Autostep is only descent when the transfer from weight perturbation to measured teacher error is positive-real-ish. Unmodeled phase >90° at an excited frequency flips the effective gradient sign there; weights grow along that mode. Projection stops the *escape to infinity* but converts it to sustained weight-pinning at ±authority with an output oscillating at the clamp — Rohrs' failure with a saturation nonlinearity added, not removed.

**Symptom.** Weights parked at the projection bound (a directly observable flag the telemetry should export); limit-cycle at a frequency unrelated to the disturbance; error *worse with the brain* only in a narrow band. The watchdog will eventually catch outcome cost, but slowly (see F10).

**Mitigations & gaps.** σ-mod exists on fast weights only. The slow layer (μS=0.005, no leak) is the classical unprotected integrator: Rohrs drift will accumulate *there*, laundered through consolidation. Minimum fix: e-modification (leak proportional to |error|, so it doesn't bias converged solutions) on the slow layer, plus a per-update dead zone sized to the measured noise floor. Also instrument "fraction of weights at clamp" as a first-class health metric.

## F2. Parameter drift and bursting under low excitation — the signature slow failure

**Literature.** B.D.O. Anderson, "Adaptive systems, lack of persistency of excitation and bursting phenomena," Automatica 21(3), 1985 — the canonical description: without persistent excitation (PE), estimates drift under noise through unexcited parameter subspace; when they cross a stability boundary the loop bursts into oscillation; the burst is itself exciting, re-converges the estimates, quiescence resumes; repeat with a period of hours-to-weeks. Sethares, Lawrence, Johnson & Bitmead, ["Parameter drift in LMS adaptive filters,"](https://ieeexplore.ieee.org/document/1164874/) IEEE Trans. ASSP 34(4), 1986 — LMS specifically: drift is *slow, non-exponential* escape along "decaying-excitation" modes while every observable error stays small. Mareels & Bitmead, Automatica 1986/88: the bursting orbit can be chaotic. Critically, σ-modification does not fully immunize: [bursting persists under forgetting-factor/σ-type laws](https://ieeexplore.ieee.org/document/4789097/) (Hsu & Costa, IEEE TAC 1987 and CDC follow-ons) — the leak trades bias for a different limit cycle. PE conditions: Boyd & Sastry, Automatica 1986.

**Trigger here — and it is structural.** FEL *manufactures* its own excitation collapse: the teacher is the host controller's residual output; as the brain succeeds, u_pid → noise floor. Post-engagement, the design is in Anderson's low-excitation regime *by definition of success*. A 64–320-unit random basis excited by a small quiescent orbit has a large effectively-null subspace; measurement noise (correlated with the teacher through the controller — see F4) random-walks the weights through it. Autostep makes it worse in one specific way: per-weight step sizes were adapted upward during the rich learning phase and remain high in directions that are now unexcited (Autostep's normalizer decays only with new data in that direction).

**Symptom.** Weeks of perfect behavior, then an unprovoked oscillation burst that self-heals — the classic field report "it went unstable for 40 seconds and fixed itself, cannot reproduce." Bench-invisible: 240 s × 10 seeds cannot exhibit a phenomenon whose quiescent phase is 10⁵–10⁷ steps. **Nothing in the current findings ledger bounds this.**

**Interaction with the growth trigger.** The whiteness trigger fires on "converged weights AND autocorrelated residual." A burst *is* an autocorrelated residual arriving right after apparent convergence — the trigger's precise firing condition. Growth during a burst permanently allocates units that memorize the transient. Add a burst veto: suppress growth (and ideally learning) when short-window output variance exceeds k× its long-window median.

**Mitigations.** (1) Excitation monitor: cheap running estimate of min-eigenvalue proxy (e.g., trace-normalized feature covariance) gating adaptation off below threshold — the "adaptation freeze" every deployed ANC product ships. (2) Dead zone at the noise floor (Peterson & Narendra). (3) Leak/e-mod on slow weights (F1). (4) Directional forgetting (Kulhavý, 1984/87) rather than uniform: forget only in excited directions. (5) A dedicated *long-run* bench mode: 10⁶+ steps, tiny disturbance, sensor noise on — measure weight-norm drift rate directly. This is the single most valuable new instrument this review can recommend.

## F3. Catastrophic interference: one weight vector, two plants

**Literature.** McCloskey & Cohen 1989; French, "Catastrophic forgetting in connectionist networks," TiCS 3(4), 1999 — interference severity scales with *input similarity × target dissimilarity*: worst case is near-identical inputs demanding different outputs. CMAC's local receptive fields limit interference (Albus 1975; convergence: Wong & Sideris 1992; Parks & Militzer 1991) but hashed/random-basis variants reintroduce it via collisions. Multiple-model adaptive control is the classical answer: Narendra & Balakrishnan, "Adaptive control using multiple models," IEEE TAC 42(2), 1997; supervisory switching with hysteresis/dwell time: Morse; Hespanha & Morse 1999.

**Trigger here.** Alternating plants/opponents/terrains with a context vector that does **not** encode plant identity: the crooked-wheel car whose crookedness flips sign, two fighter opponents with opposite punishes for the same state, terrain regimes with identical local geometry but different winds.

**Mechanism — the disambiguation criterion made precise.** The readout is linear in φ(ctx). Two tasks A/B interfere destructively exactly when their feature distributions overlap (φ_A ≈ φ_B on shared support) while their conditional targets differ. Then the fixed point is the exposure-time-weighted average of the two inverse models — which for sign-opposed corrections is *worse than nothing on both plants*. Context disambiguates iff plant identity is φ-separable, which for FAN=4 random projections requires identity to be present in ctx (a regime channel, a slow observable like trim estimate) — random features cannot conjure it from state alone when the plants occupy the same state region. The maze finding ("learns nothing when context can't see the answer") is the benign half of this coin; the malignant half is *learning the average of two visible-but-unlabeled answers*, which the bench never tests: **no current scenario alternates two plants under one brain**. That scenario should exist and is this design's sharpest falsifiable risk.

**Two-rate twist.** Smith, Ghazizadeh & Shadmehr (PLoS Biol 4(6), 2006) two-rate dynamics produce *spontaneous recovery*: after A→B training and brief washout, the slow state re-expresses A. In motor science that's the phenomenon being modeled; in a controller it means the consolidated wrong plant resurfaces minutes after apparent re-adaptation. Symptom: post-switch performance recovers via the fast layer, then *degrades again* as fast decays and stale slow weights dominate.

**Mitigations.** Regime-observable context channels (cheap, first line); per-regime brains under a supervisory switch with dwell time (Narendra-Balakrishnan); similarity-gated consolidation — block fast→slow transfer when fast and slow disagree strongly (a disagreement metric the two-rate structure gives for free); at fleet level, cluster-then-merge (F9).

## F4. Learning on correlated noise: the FEL teacher is not exogenous

**Literature.** Bias of LMS/least-squares when regressor and disturbance are correlated: errors-in-variables (Söderström, Automatica 43(6), 2007 survey; Ljung, *System Identification*); fixes: instrumental variables, total least squares. In the cerebellar lineage this is precisely Porrill & Dean's argument for *decorrelation control* (Dean, Porrill et al., 2002–2007): the adaptation target should be the decorrelation of sensory error from motor command, and naive FEL inverts the noise path. In ANC the same disease is handled by leakage and control-effort weighting (Kuo & Morgan, *Active Noise Control Systems*, 1996).

**Trigger here.** Any measurement noise n on the states feeding both ctx and the host controller — i.e., every real deployment.

**Mechanism.** Teacher u_fb = C(e_true + n); features φ(x_meas) share the same n. E[φ·u_fb] acquires a term E[φ(x+n)·C(n)] ≠ 0: the brain provably converges to reproducing the *controller's noise response*, injecting band-passed noise into the plant with gain that adds to C's own. Two aggravations specific to this design: (1) **the competence gate rewards it** — the gate opens on prediction-matches-teacher, and learning the noise-correlated component *improves* teacher mimicry while degrading the plant; the gate criterion is anti-aligned with plant benefit in exactly this case. Finding 25 (maze harm behind an open gate) is this fault class's episodic cousin; the outcome watchdog (finding 31) is the correct family of fix, but it caps harm at 1.5× — noise feedthrough that costs 1.3× sails through forever. (2) The eligibility trace low-passes φ, which *helps* (decorrelates against high-frequency n) — accidental, worth making deliberate.

**Symptom.** Higher control-signal variance and actuator activity with the brain engaged, output error unchanged or slightly worse in the noise band; weights carrying energy at frequencies where the reference has none.

**Mitigations.** Delayed-regressor instrumental variable (use φ(t−k) with k past the noise correlation time as the update regressor — one-line change to the update, classical IV); teacher low-passing; effort penalty via leakage on both layers in the noise band; and an outcome-cost term that includes actuation energy, not just tracking error — otherwise the watchdog is blind to this fault by construction.

## F5. Adaptation vs plant delay: where plain FEL breaks and FxLMS is the fix

**Literature.** Morgan, IEEE Trans. ASSP 28(4), 1980 (the filtered-x insight); Widrow & Stearns 1985; Kuo & Morgan 1996. Convergence of FxLMS requires the secondary-path *model* phase error to stay [within ±90°](https://www.academia.edu/2857461/Stability_analysis_of_adaptation_process_in_FxLMS_based_active_noise_control); equivalently, plain (unfiltered) LMS-through-a-plant is stable only where the plant phase from correction to measured error is within ±90° (Boucher, Elliott & Nelson, 1991, on plant-model error effects; Ardekani & Abdulla for general convergence analysis). FEL-specific: [Miyamura & Kimura, Systems & Control Letters 45(4), 2002](https://www.sciencedirect.com/science/article/abs/pii/S0167691101001918) prove FEL stability under a strict-positive-realness condition on the tracking-error dynamics — a pure delay violates SPR at high frequency *always*; Nakanishi & Schaal, Neural Networks 17(10), 2004 (passivity view); [FEL for plants with time delay](https://www.sciencedirect.com/science/article/pii/S1474667015390406) treats the extension explicitly.

**Quantified break condition.** For loop delay Δ (transport + actuator + one control period), the adaptation gradient sign flips at frequencies above f* = 1/(4Δ) (90° of pure delay). Numbers for this bench: dt=0.02 s and 3 steps of effective lag → Δ=60 ms → f*≈4.2 Hz — and the Hopf oscillator bank's top channel sits at 2.7 Hz with adapted frequency free to climb. **This is a concrete candidate mechanism for finding 28 (oscillators hurt balance):** the oscillator channels concentrate feature energy exactly where loop phase approaches the anti-gradient region, so those weights adapt with wrong or rotating sign, pumping the pendulum's mid-band. Testable in one afternoon: measure the phase from brain-output injection to teacher signal at 0.3/0.9/2.7 Hz in `balance`; if 2.7 Hz is past ~70°, the mechanism is confirmed and *filtered-x on the oscillator channels alone* (filter each channel through a 2-parameter plant-lag model) should flip finding 28's sign.

**Symptom.** Low-frequency error improves while a specific higher band slowly grows; per-frequency weight rotation (weights on periodic features precessing rather than converging).

**Mitigations.** Filtered-x with even a crude secondary-path model (a delay + gain is enough if phase error stays <90° — the entire ANC industry runs on this); the existing eligibility trace is a one-pole approximation of filtered-x whose time constant should be *set to the plant lag*, not tuned as a free constant; or restrict brain bandwidth below f* (band-limit features/output).

## F6. Two adaptive systems: the brain and the host integrator fight

**Literature.** Interacting adaptive loops and hidden feedback: B.D.O. Anderson, "Failures of adaptive control theory and their implications," Communications in Information & Systems 5(1), 2005. Nonlinear dynamics of coupled estimation+control: Mareels & Bitmead 1986/88. Windup and transfer: Åström & Rundqwist, "Integrator windup and how to avoid it," ACC 1989; Hanus' conditioning technique, Automatica 1987.

**Mechanism (a): DC ownership is unidentifiable.** The PID integrator and the brain's slow layer both estimate the same quasi-constant disturbance; the sum is observable, the split is not. FEL closes a loop between them: brain takes over trim → error stays small → integrator bleeds down → teacher (which *contains* the integral term) shrinks → brain's target moves. The pair random-walks along the unobservable direction; with the fast layer's leak pulling one way and the integrator pulling the other, a slow limit cycle (period ~ 1/leak vs 1/Ti mismatch) is the generic outcome — "hunting" in a loop that looks perfectly tuned in any short window. With an *adaptive* host (self-tuner, extremum seeker, MPPT), two-estimator coupling without timescale separation is the textbook route to limit cycles and chaos (Mareels & Bitmead).

**Mechanism (b): windup at authority transitions.** When the watchdog ratchets the cap down (or the gate closes), the plant loses the brain's contribution as a step disturbance; the integrator winds up during recovery; when the brain recovers authority (<1.15× ratio), both push together — overshoot, possible watchdog re-trip: a **watchdog-integrator chatter loop** whose period is the anchor-window length. The 1.5/1.15 hysteresis helps but there is no *dwell time*; supervisory-switching theory (Morse; Liberzon) says hysteresis without dwell can still chatter under drifting cost.

**Symptom.** Ultra-slow oscillation of the u_pid/u_brain split with flat total; or watchdog engage/release cycling in logs.

**Mitigations.** Exclude the integral term from the teacher (feed P+D output only — one line; the brain then owns DC uncontested and the integrator becomes a backstop); or leak the slow layer so the integrator owns DC (pick exactly one owner). Add minimum dwell time to gate and watchdog transitions. Apply bumpless-transfer conditioning: on any authority change, back-initialize the integrator so u_total is continuous.

## F7. Frozen normalization vs distribution shift

**Literature.** Change detection: Page's CUSUM, Biometrika 1954. Covariate shift: Shimodaira, JSPI 2000. The neural analog (frozen BatchNorm statistics failing under domain shift, fixed by re-estimating them — AdaBN, Li et al., Pattern Recognition 2018) is the same fault in modern dress.

**Trigger here.** "Pure reads: normalisation adapts only on learn." Post-convergence (or gate-frozen, or watchdog-capped — precisely the states where learning is off), a sensor recalibration, firmware unit change, or mount shift moves input means/scales.

**Mechanism.** Shifted inputs through frozen normalization drive tanh units into saturation; the feature map collapses toward a binary pattern; the linear readout emits a confident, arbitrary output. The gate is already open so competence is not re-checked; the watchdog's anchor was measured under the old calibration so "pre-engagement cost" is no longer the counterfactual — the one certified safety component is comparing against a world that no longer exists.

**Symptom.** Abrupt competence loss coincident with a maintenance event; telemetry signature is unmistakable if you record it: fraction of units with |tanh|>0.99 jumps.

**Mitigations.** Always-on input sentinels independent of the learning flag (running quantiles + CUSUM per channel — bytes of state, certifiable, belongs *inside* the gate/watchdog SIL boundary, and this is the strongest concrete addition this review proposes to the certification story); on detected shift: close gate, invalidate watchdog anchor, re-enter calibration. Never let the anchor outlive a detected input-distribution epoch.

## F8. Replay of stale samples after plant change

**Literature.** Forgetting vs windup trade-off in recursive estimation (Åström & Wittenmark, *Adaptive Control*; Kulhavý's directional forgetting); off-policy staleness of experience replay under nonstationarity (Lin, MLJ 1992, noted the assumption; the modern continual-RL literature re-derives it).

**Mechanism.** The episodic replay ring (rbCap, masked) stores (ctx, residual-demand) pairs whose targets embed the *old* environment's outcome structure. After an opponent/plant change, each replayed update is a step toward the stale optimum; fresh data fights the buffer. Recovery time is set by buffer turnover, not learning rate; worse, the residual-demand teacher means stale targets are wrong by the *difference of environments*, which can exceed either environment's signal.

**Symptom.** Post-change learning curve recovers, then plateaus above pre-change error until exactly one buffer-lifetime has elapsed; replayed-batch loss diverges from fresh-sample loss (a free, logging-only detector: track the two losses separately — persistent gap = stale buffer).

**Mitigations.** Recency-weighted sampling; flush on change detection (share F7's CUSUM); epoch-tag samples with the normalization epoch and never replay across epochs; replay into the fast layer only (let consolidation see only fresh data).

## F9. Merging brains trained on different plants

**Literature.** Diffusion-LMS optimality holds for a *common* w° (Cattivelli & Sayed, IEEE TSP 58(3), 2010; Sayed, *Adaptation, Learning, and Optimization over Networks*, 2014). When nodes' optima differ, single-task diffusion converges to a precision-weighted Pareto compromise — Chen, Richard & Sayed, "Multitask diffusion adaptation over networks," IEEE TSP 62(16), 2014; clustering fixes: Zhao & Sayed 2015; Nassif et al., "Multitask learning over graphs," IEEE SPM 2020. Fusion under unknown correlation: covariance intersection (Julier & Uhlmann, ACC 1997).

**Mechanism.** Three stacked faults. (1) *Averaging across tasks*: precision-weighted averaging of inverse models for different plants yields the compromise controller; the set of stabilizing controllers is non-convex in general, so the average of two stabilizing weight vectors can stabilize neither (convexity of the closed-loop objective in w holds per-plant, not across plants). (2) *Precision ≠ correctness*: E measures accumulated excitation on the brain's own plant; a long-service plant-A veteran merging into a plant-B cell arrives with dominant precision and drags the whole cell toward A — the fleet mechanism grants authority by seniority, not by relevance. (3) **The known basis-drift debt makes this concrete today**: `_recycle` re-wires units from each brain's own RNG stream, so peer bases silently diverge; after drift, index i means *different random units* on different peers, and "exact convex averaging" becomes averaging coordinates of different coordinate systems — not biased, meaningless. Every gossip claim beyond the recycle horizon is currently unsupported.

**Symptom.** Fleet-wide simultaneous degradation after a merge round (the signature that distinguishes merge poisoning from individual faults); merged brain worse than either parent on either plant.

**Mitigations.** Basis integrity check before any merge (seed + growth/recycle log hash — reject or re-map on mismatch; cheap, and closes the drift debt's blast radius); similarity gate on weight distance under the E-metric (Mahalanobis) with a reject threshold — this is the heterogeneous-gossip frontier item, and multitask-diffusion theory says it is *required*, not optional; cluster-then-merge; covariance intersection when peer correlations are unknown (double-counting via gossip loops otherwise inflates precision); merge slow layers only.

## F10. Stability-margin erosion as the sidecar's effective gain ramps

**Literature.** Rohrs 1985 (the parasitic-dynamics half is really a margin statement); L1 adaptive control as the modern architecture that decouples adaptation rate from margins via low-pass filtering (Cao & Hovakimyan, IEEE TAC 2008); run-time assurance/Simplex (Sha, IEEE Software 2001) for the certification framing.

**Mechanism.** The engaged brain adds a parallel path; as α ramps and ||w|| grows toward authority, loop gain rises and phase margin falls — and the adaptation law itself is an extra dynamic feedback (roughly an integrator from error to gain) contributing lag. The certification decomposition ("only the gate/clamp/watchdog carries SIL") holds **only if the clamp is sized in loop-gain terms**: the projection bound limits amplitude, and a small-gain argument covers it *iff* authority is chosen ≤ the host loop's gain-margin slack at the relevant band. Authority sized from actuator range (the natural engineering instinct) certifies nothing: a bounded-amplitude signal at the wrong phase near crossover destabilizes a loop with 4 dB of margin while respecting any actuator clamp. Meanwhile the watchdog's 1.5× outcome threshold is slow against a mode growing at crossover — oscillation energy compounds for many anchor-windows before mean cost crosses 1.5×.

**Symptom.** Ringing that grows with α during the ramp; degraded disturbance rejection near the host's crossover frequency.

**Mitigations.** Written sizing rule: authority(band) ≤ host gain-margin slack(band) — this belongs in PORTING.md as a hard integration requirement, because it is the one parameter integrators will set by feel; band-limit the brain's output (L1-style filter — cheap and it simultaneously helps F5); add a second watchdog channel on narrowband output energy near crossover (fast) alongside outcome cost (slow); optional periodic micro-dither to measure the engaged loop's margin online (see F11).

---

## F11. Proving benefit with no reference twin — how deployed adaptive products do it

The bench's gray-twin methodology cannot ship. Deployed practice, in increasing strength:

1. **Shadow mode** (predict, don't actuate): the gate's silent phase is exactly this — but only *pre*-engagement. Real deployments (automotive "shadow mode" for autonomy features; aerospace monitored modes) run shadow **audits recurrently**: periodically re-enter predict-only and re-score competence against the live teacher. Cheap; catches F7-class silent rot; blind to F4 (mimicry ≠ benefit).
2. **Intermittent disengagement probes** (A/B in time): on a schedule or trigger, zero α for short paired blocks and compare outcome cost engaged vs disengaged (paired statistics kill most nonstationarity; the design's determinism helps). This is the *only* mechanism that keeps the watchdog's "without the brain" anchor truthful over time — otherwise the anchor is a one-shot measurement of a drifting world (F7, and finding 31's false-alarm lesson is the first symptom of anchor staleness). Requires bumpless transfer (F6b) and probe-cost budgeting. Process-control loop auditing has done on/off performance audits for decades.
3. **Dither probing**: continuous tiny zero-mean excitation → running estimate of closed-loop sensitivity/margins without disengaging (heritage: relay autotuning, Åström & Hägglund; plant-friendly ID). Directly measures F10's margin erosion, not just cost.
4. **Minimum-variance benchmarking from routine data**: the Harris index (Harris, Can. J. Chem. Eng. 67, 1989; industrial review: Jelali, Control Engineering Practice 14, 2006) bounds achievable variance given the loop delay using *passive* data only — a twin-free "how far from optimal" score; the standard answer in an industry that also cannot run twins.
5. **Episodic mode gets a better deal**: log action propensities and use off-policy evaluation (importance sampling, Precup et al. 2000; doubly-robust, Dudík et al. 2011) — counterfactual "what would the host policy have scored" from logged data alone.
6. **The fleet is a twin factory**: staggered engagement across N units (stepped-wedge design) yields population-level A/B with no unit fully forgoing the brain — a genuine, underexploited advantage of the fleet story; worth a scenario.

Recommended contract: watchdog anchor legally expires after T or after any F7 change-signal; renewal only by a disengagement probe. That turns the anchor from a measurement into a *maintained* quantity — and it is the piece the SIL narrative currently lacks.

---

## Priority ranking against THIS bench, with the cheapest discriminating experiment each

| # | Fault | Why it bites here first | Discriminating experiment |
|---|-------|------------------------|---------------------------|
| 1 | F5 delay/anti-gradient | Likely already visible as finding 28 | Measure injection→teacher phase at 0.3/0.9/2.7 Hz in `balance`; filtered-x the oscillator channels |
| 2 | F2 bursting | Structurally guaranteed by FEL success + no slow leak; invisible at 240 s | 10⁶-step quiescent run, sensor noise on, log weight-norm drift + clamp-fraction |
| 3 | F3 interference | No alternating-plant scenario exists; two-rate re-expression untested | New scenario: car with sign-flipping crookedness, identity absent from ctx, then present |
| 4 | F9+basis drift | Known, unmeasured debt that voids the merge-exactness claim | Long gossip run; hash bases; measure merge quality vs recycle count |
| 5 | F4 correlated noise | Gate criterion is anti-aligned with benefit in this case | Add sensor noise to `balance`; compare actuation variance ±brain; test IV (delayed-regressor) update |
| 6 | F7/F11 anchor staleness | Watchdog is the product; its counterfactual currently decays silently | Mid-run sensor rebias in `launcher`; check watchdog false-negative; add CUSUM sentinel |
| 7 | F6 integrator fight | Slow; masked by short runs | Long `path` run logging u_pid/u_brain split spectrum; test P+D-only teacher |
| 8 | F1/F10 margins | Real-hardware risk more than bench risk | Add parasitic actuator lag pole to one scenario; verify authority-vs-gain-margin sizing rule |

Cross-cutting instrument recommendations (each serves ≥2 faults): clamp-fraction and saturation-fraction telemetry (F1, F7); short/long-window variance ratio as burst veto on growth and learning (F2, F5); fresh-vs-replayed loss gap (F8); CUSUM input sentinels inside the certified boundary (F7, F11); dwell timers on gate/watchdog transitions (F6, F11); basis hash in the merge handshake (F9).

Sources (verified this session): [Sethares et al. 1986, IEEE Xplore](https://ieeexplore.ieee.org/document/1164874/) · [Bursting under forgetting-factor/σ-type adaptation, IEEE](https://ieeexplore.ieee.org/document/4789097/) · [Miyamura & Kimura 2002, Systems & Control Letters](https://www.sciencedirect.com/science/article/abs/pii/S0167691101001918) · [FxLMS ±90° stability analysis](https://www.academia.edu/2857461/Stability_analysis_of_adaptation_process_in_FxLMS_based_active_noise_control) · [FEL with time delay, IFAC](https://www.sciencedirect.com/science/article/pii/S1474667015390406) · [Nakanishi & Schaal 2004](https://dl.acm.org/doi/10.1016/j.neunet.2004.05.003). Remaining citations are canonical (full venues given inline).