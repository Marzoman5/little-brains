# Next brain parts after the cerebellum — a menu of classical, MCU-compatible architectures

Framing citation for the whole menu: Doya (1999), "What are the computations of the cerebellum, the basal ganglia and the cerebral cortex?", *Neural Networks* 12:961–974. Doya's division of labor — cerebellum = supervised learning of internal models, basal ganglia = reinforcement-based action selection, cortex = unsupervised state representation — is the roadmap this report follows. Little Brains has built the cerebellum. The owner's four questions map cleanly: (a) multiple opponents/plants = **multiple internal models with responsibility estimation** (still cerebellar, MOSAIC); (b) "will" = **basal ganglia selection**; (c) many motors as one act = **coupled CPGs + microcomplex hierarchy**; (d) knowing the total goal = **GVF forecast layer + micro-MPC + goal/habit arbitration**. Everything below is pre-deep-learning classical, linear-in-frozen-features, deterministic, and sized for an M0+/ESP32.

Cost baseline for all estimates: current BrainV2 at n=256 units, FAN=4, nOut=2, two-rate float32 ≈ 8 KB weights + 2 KB basis indices; one step ≈ n·FAN MACs + n tanh (LUT on M0+). ESP32 (240 MHz, FPU): ~2–5 µs/step. M0+ (48 MHz, Q15 CMSIS-DSP): ~60–120 µs full-basis, ~5–15 µs if only the Golgi-sparse active set (k ≈ 32) is evaluated. "Extra cost" below is on top of this.

---

## 1. MOSAIC — paired forward/inverse models with responsibility estimation

**The classical answer to (a): 2+ opponents/plants/terrains in one brain.**

Primary: Wolpert & Kawato (1998), "Multiple paired forward and inverse models for motor control," *Neural Networks* 11(7–8):1317–1329. Haruno, Wolpert & Kawato (2001), "MOSAIC model for sensorimotor learning and control," *Neural Computation* 13(10):2201–2220. Statistical ancestor: Jacobs, Jordan, Nowlan & Hinton (1991), "Adaptive mixtures of local experts," *Neural Computation* 3:79–87. Control-theoretic twin with actual stability proofs: Narendra & Balakrishnan (1997), "Adaptive control using multiple models," *IEEE Trans. Automatic Control* 42(2):171–187.

**Mechanism.** M modules, each a (forward model, inverse model) pair. Every step, each forward model predicts the next sensory outcome; the responsibility signal λ_i is a normalized softmax of prediction likelihood, λ_i ∝ prior_i · exp(−|x − x̂_i|²/2σ²), low-pass filtered. λ does double duty: it **gates control** (total command = Σ λ_i u_i) and **gates learning** (each module's weights update scaled by λ_i) — so the module that predicts this plant best gets both the authority and the training data. This is simultaneously the fix for context switching AND for catastrophic interference: opponent B's data never overwrites opponent A's weights because A's responsibility collapses to ~0 when B is present.

**What it buys over the current cerebellum.** Today, switching from crooked-wheel to icy-road (or opponent A to opponent B) forces relearning through the same weight vector — the fast layer thrashes and the slow layer slowly poisons. MOSAIC gives instant (one-to-few-step) recognition-and-switch, retention of both solutions, and a free novelty detector: uniformly low likelihood across all modules = "plant I've never seen" — a principled trigger for spawning a module (growth trigger, one level up) and a natural input to the competence gate.

**MCU cost.** The crucial trick: all M modules share the ONE frozen basis — a module is just extra readout rows. M=4 modules, nOut=2, forward model predicting 4 context channels: inverse weights 4×2×256×4B = 8 KB, forward weights 4×4×256×4B = 16 KB (float32; halve for Q15; halve again slow-layer-only per module with one shared fast layer). Compute: M forward dot-products over the k≈32 active units + M exp() (or a 1/(1+z²) rational surrogate) ≈ 1–3 µs extra on ESP32. Well inside 35 KB with M=2–4; M=8 needs Q15.

**Composition with gate/fleet.** Best-in-class fit. Each module's weight vector still lives in the same convex coordinates, so fleet merging works *per module*, and responsibility statistics are exactly the precision weights the merge already wants. Per-module competence gating is natural: a module engages only after its forward model beats the naive predictor — the existing gate contract, replicated M times. The certifiable core stays tiny: the responsibility computation + mixing is a few hundred bytes of code, and Narendra & Balakrishnan is the citation trail for provable stability of switched multiple-model adaptive control.

**Biggest risk.** Responsibility estimation collapse: with σ mis-set or plants that look identical in the observed channels, either one module wins everything (back to interference) or λ dithers and the blended command excites both plants' wrongness. Haruno 2001 needed responsibility priors from context cues to stabilize it. Mitigation: hysteresis on λ (see BG section — this is exactly what selection dynamics are for) and the maze lesson (finding: context that can't see the discriminating variable can't switch on it — by design safe, but test it deliberately).

---

## 2. Basal ganglia action selection — the "will"

**The classical answer to (b): goal-directed action selection, "wishing to be correct."**

Primary: Redgrave, Prescott & Gurney (1999), "The basal ganglia: a vertebrate solution to the selection problem?", *Neuroscience* 89(4):1009–1023. Gurney, Prescott & Redgrave (2001), "A computational model of action selection in the basal ganglia," parts I & II, *Biological Cybernetics* 84:401–410 and 411–423. Embodied on a robot: Prescott, Montes-González, Gurney, Humphries & Redgrave (2006), "A robot model of the basal ganglia: behavior and intrinsic processing," *Neural Networks* 19:31–61. The learning half: Barto, Sutton & Anderson (1983), "Neuronlike adaptive elements that can solve difficult learning control problems," *IEEE Trans. SMC* 13:834–846 — the actor-critic, on exactly the kind of tiny fixed basis Little Brains uses.

**Mechanism.** Reframe "will" as the *selection problem*: K candidate behaviors compete for a shared motor resource; the BG is a central switch that (i) computes salience for each channel (linear in context — one dot product each on the frozen basis), (ii) selects via off-center/on-surround competition, (iii) exhibits **hysteresis / persistence** — the currently selected action gets a bonus, so the animal doesn't dither between two nearly-equal options (the GPR papers analyze this switching property explicitly; Prescott 2006 shows it producing coherent behavioral sequencing on a robot). Learning the saliences is a linear actor-critic: critic = one value weight-vector, actor = K preference vectors, TD error trains both. "Wishing to be correct" = TD error, literally: dopamine as the difference between expected and obtained outcome.

**What it buys.** The current brain modulates a command *inside* one behavior; it has no machinery for choosing *between* behaviors (chase vs retreat vs reload in the fighter; track-line vs pull-over-and-recalibrate in the car). A BG layer gives clean commitment, no chattering, interruptibility only by sufficiently better options, and a value signal that is the seed of "total goal" (section 7/8).

**MCU cost.** Tiny — the smallest item on this menu. K=4–8 channels: K+1 weight vectors ≈ K×256×4B = 4–8 KB (or share the active-set evaluation, making incremental cost K dot-products over 32 units ≈ sub-µs). The GPR selection dynamics are ~20 lines of fixed arithmetic; state is K floats.

**Composition with gate/fleet.** The selection layer is small, deterministic, and enumerable — it belongs on the *certified* side of the certification decomposition, next to the gate/clamp/watchdog: the learner proposes saliences (uncertified), the selector's switching logic (certified, few hundred bytes) enforces persistence, mutual exclusion, and a hardwired "do-nothing wins if all saliences are low" default — which is the competence gate generalized from one action to K. Fleet merging: preference vectors are again coordinates in the shared convex problem. Warning from the ledger: finding 21 (one-step TD bootstrap failed on the fighter) says the critic must be introduced carefully — start with Monte-Carlo/outcome returns before bootstrapping, and mind the gate's off-policy trap (Baird).

**Biggest risk.** Reward specification. FEL gets its teacher for free from the reflex; a critic needs a scalar reward, and a wrong reward is a confident wrong will. The outcome watchdog (finding 31) is the right containment — the host's own cost metric is already wired in as the harm anchor; make it the reward and the two contracts share one signal.

---

## 3. Forward models / Smith predictor — delay compensation

Primary: Smith (1957), "Closer control of loops with dead time," *Chemical Engineering Progress* 53:217–219. The cerebellar reading: Miall, Weir, Wolpert & Stein (1993), "Is the cerebellum a Smith predictor?", *Journal of Motor Behavior* 25(3):203–216. Context: Wolpert, Miall & Kawato (1998), "Internal models in the cerebellum," *Trends in Cognitive Sciences* 2(9):338–347.

**Mechanism.** Learn a forward model f̂(ctx, u) → predicted outcome with the *same frozen-basis + linear-readout trick* (the teacher is the plant itself — next observation; pure supervised, no new learning machinery). Then control against the *predicted, undelayed* plant while a delay-line comparison of prediction vs delayed measurement corrects drift. The learner's error signal is also de-aliased: FEL credit currently lands on the wrong time-step whenever the plant has dead time (this is the same disease FxLMS treats in adaptive filtering — the curriculum already lists it).

**What it buys.** Every scenario with actuator/sensor lag (flight with plant lag, network/ops loops, any real hardware over a bus) currently forces the brain to learn a smeared, phase-shifted mapping. A Smith structure converts dead time from a stability limit into a buffer lookup. It is also the *entry drug* for model-based anything: the forward model built here is reused verbatim by micro-MPC (section 5) and MOSAIC (section 1's forward half).

**MCU cost.** Forward weights nStates×256×4B ≈ 4 KB for 4 predicted channels; delay ring buffer d×nOut floats (d = delay in steps, typically 2–50 → 16–400 B). One extra model evaluation per step ≈ 1–3 µs ESP32.

**Composition.** The forward model has zero authority — it never actuates — so it needs no gate; it inherits determinism and mergability trivially. Its prediction error is furthermore a superior competence signal for the existing gate ("do I understand this plant?" measured directly rather than via teacher-matching).

**Biggest risk.** Classical and well-documented: Smith predictors are sensitive to delay/model mismatch — an underestimated delay can destabilize the compensated loop faster than no compensation. Containment: watchdog anchor already covers "worse than without it"; additionally bound the correction authority exactly like brain output. And note finding 13's ghost: feeding *live-learning* model outputs back as control context failed before — converge, freeze, then trust.

---

## 4. Coupled CPG networks — many motors, one act

**The classical answer to (c).**

Primary: Ijspeert, Crespi, Ryczko & Cabelguen (2007), "From swimming to walking with a salamander robot driven by a spinal cord model," *Science* 315:1416–1420. Ijspeert (2008), "Central pattern generators for locomotion control in animals and robots: a review," *Neural Networks* 21:642–653. The adaptive-oscillator substrate already in the repo: Righetti, Buchli & Ijspeert (2006), "Dynamic Hebbian learning in adaptive frequency oscillators," *Physica D* 216:269–281.

**Mechanism.** Take the existing Hopf oscillators (V2 already carries 3 adaptive-frequency channels) and add *phase coupling*: dθ_i/dt = ω_i + Σ_j w_ij r_j sin(θ_j − θ_i − φ_ij). The coupling matrix (w_ij, φ_ij) encodes the gait; a single scalar **drive** input sets frequency/amplitude and — Ijspeert 2007's headline result — crossing a drive threshold switches the whole body between gaits (swim↔walk) with smooth transients, limit-cycle stability, and graceful sensory perturbation recovery. Per-joint output = oscillator state through a small learned shaping readout (frozen basis again; the cerebellum learns *corrections on top of* the CPG, which is the biological arrangement).

**What it buys.** Currently nOut>1 outputs are independent readouts sharing a basis — nothing enforces coherent phase relationships between motors, and the oscillator drive uses the MEAN teacher across outputs (flagged in the audit as a hack, and finding 28 says oscillators actively hurt balance). Coupled CPGs give: one command variable controlling a whole coordinated pattern; phase relationships guaranteed by dynamics rather than learned; dimensionality collapse (a 12-motor gait becomes a 1–3 dimensional control problem for the layers above — exactly what the BG selector and micro-MPC need to stay tiny).

**MCU cost.** Trivial: J oscillators = 2J state floats + J×J coupling table (J=6: ~150 B state, 288 B table); a few hundred flops per step, sub-µs. This is the cheapest capability-per-byte item on the menu.

**Composition.** CPG dynamics are deterministic, bounded (Hopf amplitude is intrinsically clamped), and analyzable — limit-cycle stability is provable, so amplitude/frequency clamps sit naturally on the certified side. Fleet: coupling tables are small and discrete enough to ship in instinct files. Direct answer to open concern #6: the finding-28 balance regression is likely *because* uncoupled oscillators inject phase-incoherent drive; coupling plus per-output (not mean) teacher drive is the principled fix to test.

**Biggest risk.** Scope honesty: CPGs pay off only for rhythmic/multi-actuator plants; none of the current nine scenarios has >2 coordinated motors. Building this ahead of a hexapod/quadruped scenario would be architecture without a bench — build the scenario first (an ESP32 hexapod or 3-joint hopper scenario would also be the physical-demo video the handoff doc wants).

---

## 5. Tiny model-based lookahead — micro-MPC / Dyna

Primary: Sutton (1990), "Integrated architectures for learning, planning, and reacting based on approximating dynamic programming," *ICML* 216–224, and Sutton (1991), *SIGART Bulletin* 2(4):160–163 (Dyna). Stability/certification trail for receding-horizon control: Mayne, Rawlings, Rao & Scokaert (2000), "Constrained model predictive control: stability and optimality," *Automatica* 36:789–814.

**Mechanism.** Reuse the section-3 forward model. At a decision point, enumerate a small candidate action set (K=3–8 discrete, or 5–9 grid points on a continuous axis), roll each 1–3 steps through the frozen-basis forward model, score with a learned cost readout (or GVF forecasts, section 7), pick the argmin — with the incumbent action favored by a switching cost (hysteresis again). Dyna's insight adds free sample-efficiency: replay the model between real steps to train the reactive policy — and V2 *already has* the replay ring in episodic mode; Dyna is that ring pointed at a learned model instead of stored transitions.

**What it buys.** The first genuinely *anticipatory* capability: "if I commit to A, where am I in 3 steps?" — the beginning of the owner's (d), "knowing the total goal," expressed as minimizing predicted future cost rather than imitating a reflex. Also the goal-directed half of section 8's arbitration.

**MCU cost.** Compute-bound, not memory-bound: extra memory ≈ 0 beyond the forward model + one cost readout (1 KB). Compute: K×depth model evals — 8×3×~2 µs ≈ 50 µs on ESP32. Fine at episodic decision points (dt ≥ 0.1 s: thousands of times headroom); NOT affordable inside a 1 kHz continuous loop on M0+. Deploy it in the episodic regime only — which is exactly where the current architecture is weakest (launcher 38.9%, maze barely at parity).

**Composition.** Superb certification story: because the action set is enumerated and the model frozen, the planner is deterministic and exhaustively testable; better, it slots into the Simplex/run-time-assurance pattern (Sha 2001, "Using simplicity to control complexity," *IEEE Software* 18(4):20–28) — uncertified planner proposes, certified gate/watchdog disposes, reflexive controller is the assured fallback. That is the existing safety contract, unchanged, with a smarter proposer behind it.

**Biggest risk.** Compounding model bias — the deep-RL failure mode exists at every scale, and the ledger already paid for the lesson once (finding 13: live-learned predictor outputs as inputs measurably failed; finding 21: bootstrap failed). Rules: freeze the model before planning over it; keep horizons ≤3; gate planning authority on forward-model prediction error (only plan where the model demonstrably predicts).

---

## 6. Hierarchical cerebellar microcomplexes — one brain per part, plus a coordinator

Primary: Ito (1984), *The Cerebellum and Neural Control*, Raven Press; Ito (2008), "Control of mental activities by internal models in the cerebellum," *Nature Reviews Neuroscience* 9:304–313. Apps & Garwicz (2005), "Anatomical and physiological foundations of cerebellar information processing," *Nature Reviews Neuroscience* 6:297–311 — the microzone/microcomplex evidence: the cerebellum is not one learner but thousands of small, parallel, *privately-errored* modules, each owning a specific input–output mapping. Hierarchical MOSAIC: Haruno, Wolpert & Kawato (2003), "Hierarchical MOSAIC for movement generation," *International Congress Series* 1250:575–590.

**Mechanism.** Instead of one 320-unit brain with nOut=6, run six 64-unit brains each owning one actuator with its own context subset, own gate, own watchdog — plus one upstream brain whose "plant" is the ensemble: it sees slow/abstract context and its output is *bias to the lower gates and setpoints*, trained on the residual the lower layer can't remove (upper layers correct what lower layers systematically miss — the layered-brains road already named in the handoff).

**What it buys.** (c) at the structural level: per-subsystem locality (the elevator brain can't corrupt the aileron brain), per-module certification (each gate/watchdog is independently rated — the certification decomposition multiplies instead of complicating), per-module fleet exchange (merge only the wheel-brain across a fleet with different chassis), and graceful partial failure. Memory usually *drops*: six sparse 64-unit brains ≈ one 384-unit brain in cost but each search problem is 6× smaller and interference-free by construction.

**MCU cost.** Roughly cost-neutral vs one big brain at equal total units; small per-module overhead (gate state, watchdog anchor: tens of bytes each).

**Composition.** This *is* the gate/fleet story, tiled. One caution for fleets: per-module merging requires per-module shared bases — the known `_recycle` basis-drift debt becomes M times more important.

**Biggest risk.** Inter-layer credit assignment — the one problem the ledger has failed at twice (findings 13, 21). The safe discipline, from those findings: strictly bottom-up staging (lower modules converge and their gates open BEFORE the upper layer trains), upper layer trains on residual only, no live circular dependencies. Hierarchy without that discipline is finding 13 at scale.

---

## 7. GVF / nexting forecast layer — cheap "knowing what comes next"

Primary: Sutton, Modayil, Delp, Degris, Pilarski, White & Precup (2011), "Horde: a scalable real-time architecture for learning knowledge from unsupervised sensorimotor interaction," *AAMAS* 761–768. Modayil, White & Sutton (2014), "Multi-timescale nexting in a reinforcement learning robot," *Adaptive Behavior* 22(2):146–160. Applied on embedded prosthetics — the project's declared closest relative: Pilarski, Dawson, Degris, Carey, Chan, Hebert & Sutton (2013), "Adaptive artificial limbs: a real-time approach to prediction and anticipation," *IEEE Robotics & Automation Magazine* 20(1):53–64.

**Mechanism.** A general value function is a tiny question: "what will signal X accumulate/become, discounted at timescale γ?" Each GVF = one weight vector on the existing frozen basis, trained by TD(λ) from the signal itself — self-supervised, no reward design, no policy. A bank of G predictions (near-term error, contact force, load, opponent distance at 0.1/1/10 s horizons) runs in parallel. Modayil et al. ran *thousands* in real time on a robot; an MCU runs 8–32 easily.

**What it buys.** The cheapest possible "knows what's coming": anticipatory features for the main brain (predicted-future-error as context lets the cerebellum act before the error exists), an anticipatory harm watchdog (predicted cost rising → shrink authority *before* the outcome ratchet trips — a direct upgrade path for the finding-31 mechanism), and cost forecasts to score micro-MPC rollouts without deep rollouts. Also the honest first step toward "total goal": a goal you can state as "keep predicted future cost low" needs exactly this layer.

**MCU cost.** G×256×4B = 1 KB per GVF float32 (G=8: 8 KB; Q15: 4 KB); per-step cost G sparse dot-products + trace updates ≈ 1–2 µs ESP32 for G=8.

**Composition.** Zero authority — predictions never actuate — so no gate needed; deterministic; weight vectors merge across fleets like everything else (fleet-shared forecasts: one robot's learned "this terrain precedes wheel slip" ships to peers). Freeze-then-feed (finding 13) applies when predictions become another learner's inputs.

**Biggest risk.** Off-policy divergence when predictions are conditioned on policies not being followed (Baird's counterexample — already flagged in the handoff curriculum); mitigations are on-policy nexting only (Modayil's variant is stable in practice) or true-online TD(λ) which is already on the study list. Secondary risk: feature-poverty — a GVF through a 4-fan random basis can only predict what the basis can see; expect maze-like "can't see it, can't next it" boundaries, and treat them as the same by-design safety.

---

## 8. Habit vs goal — dual controllers with uncertainty-based arbitration

Primary: Dickinson (1985), "Actions and habits: the development of behavioural autonomy," *Phil. Trans. R. Soc. Lond. B* 308:67–78. Daw, Niv & Dayan (2005), "Uncertainty-based competition between prefrontal and dorsolateral striatal systems for behavioral control," *Nature Neuroscience* 8:1704–1711. Keramati, Dezfouli & Piray (2011), "Speed/accuracy trade-off between the habitual and the goal-directed processes," *PLoS Computational Biology* 7(5):e1002055.

**Mechanism.** Run both controllers the menu now contains — the cheap reactive learner (habit: the existing cerebellum + actor) and the expensive planner (goal: micro-MPC over the forward model) — and arbitrate per decision by *relative uncertainty* (Daw: whichever system's value estimate is currently more reliable wins) or by *value of computation* (Keramati: plan only when the expected gain from planning exceeds its cost — literally CPU-budget-aware on an MCU). Early in learning and after plant changes, the planner leads; as the habit's predictions tighten, control transfers to the microsecond path. This is Dickinson's behavioral autonomy: goals compile into habits.

**What it buys.** The owner's (d) closed properly: a system that *knows the total goal* (holds it explicitly as the planner's cost function) yet doesn't pay planning costs at 1 kHz, plus automatic re-engagement of deliberation exactly when the world changes. It is also the natural umbrella: MOSAIC picks *which world*, BG picks *which act*, GVFs say *what's coming*, MPC says *what serves the goal*, arbitration says *who's in charge right now*.

**MCU cost.** Near-zero beyond its constituents: two uncertainty accumulators (per-system squared prediction-error filters, a handful of floats) and a comparison. The uncertainty substrate idea already in V2-FEATURES (K1-style per-weight variance) is exactly the right raw material — one mechanism, now with a fourth consumer.

**Composition.** The arbitration comparator is the gate contract generalized: today's gate arbitrates {reflex alone} vs {reflex+brain} by competence; this arbitrates {habit} vs {planner} by uncertainty. Same shape, same few-hundred-byte certifiable core, same watchdog anchor. It should literally be the same code path.

**Biggest risk.** It is the most integrative item — it cannot be built before sections 3, 5, and ideally 7 exist and are individually trusted. Two learners plus an arbitrator is a new interference surface, and both systems' uncertainty estimates must be *calibrated against each other* or arbitration oscillates; Daw 2005 leans on explicit posterior variances, which the frozen-basis linear setting supplies cheaply (that is the K1/LinUCB substrate) — but it must be validated on the bench before authority flows through it.

---

## Summary table

| Part | Answers | Extra RAM (M=4/G=8-scale) | Extra µs/step (ESP32) | Gate/fleet fit | Kill risk |
|---|---|---|---|---|---|
| 1. MOSAIC | (a) switching, interference | 8–24 KB | 1–3 | per-module gates; per-module merge | responsibility collapse/dither |
| 2. Basal ganglia selection | (b) will | 4–8 KB | <1 | selector joins certified core | reward mis-specification |
| 3. Smith predictor | latency (enables 1,5,8) | 4–5 KB | 1–3 | zero-authority; better gate signal | delay mismatch instability |
| 4. Coupled CPGs | (c) multi-motor | <1 KB | <1 | provable limit cycles; instinct-file-able | no multi-motor scenario yet |
| 5. Micro-MPC / Dyna | (d) lookahead | ~1 KB | ~50 at decision pts | Simplex: propose/dispose | compounding model bias |
| 6. Microcomplex hierarchy | (c) structure, certification | ~neutral | ~neutral | the gate story, tiled | inter-layer credit (finding 13) |
| 7. GVF/nexting | (d) what comes next | 4–8 KB | 1–2 | zero-authority; merge-able | off-policy divergence; feature poverty |
| 8. Habit/goal arbitration | (d) total goal | <1 KB | <1 | gate contract generalized | needs 3+5+7 first; calibration |

## Recommended sequencing

Dependency-honest order: **3 → 7 → 1 → 2 → 5 → 8**, with **4** gated on building a multi-motor scenario and **6** as the packaging that emerges once 1–2 exist. The Smith-predictor forward model (3) is the keystone — one supervised learner, same frozen-basis trick, teacher free from the plant — that MOSAIC's recognizers (1), micro-MPC (5), and the arbitration (8) all reuse; GVFs (7) are the cheapest immediate capability and directly upgrade the finding-31 watchdog from reactive to anticipatory. MOSAIC (1) is the single highest-leverage answer to the owner's stated pain (multiple opponents/terrains) and should get a dedicated bench scenario (same plant family, parameter jumps mid-run — e.g., the car's crooked wheel angle switching among 3 values) before any code. Every item obeys the house constraints: linear-in-frozen-features, no backprop, deterministic, KB-scale, and each one's authority path terminates in the existing gate/clamp/watchdog — the safety contract is the part that never changes.