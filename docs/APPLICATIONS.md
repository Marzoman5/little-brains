# Where the cerebellum fits — deployment map, mechanics, limits

Status 2026-08-26 (V2.1). Companion research with citations:
`docs/horizons-2026-08-26/`. Bench evidence: FINDINGS.md 30-35.

## How it works, in five lines

A host controller already runs the loop. The brain reads a small context
vector (raw observables), passes it through a frozen random sparse basis
(64-320 tanh units, 4 inputs each, exact-Golgi sparsified), and a learned
two-rate linear readout adds a correction to the host's command. The
teacher is the host's own output ("learn what my reflex keeps having to
do" — feedback-error learning) or an episodic outcome residual. A
competence gate keeps it silent until predictions prove out; an outcome
watchdog withdraws authority if the host's own cost metric worsens vs its
pre-engagement baseline. Everything derives from dt and authority — no
per-application tuning.

## What an application must have (integration checklist)

1. A loop or repeated decision (5 Hz - 20 kHz, or per-event).
2. A working baseline controller or aim rule (it is the teacher).
3. Observables the correction actually depends on — including
   commanded-vs-achieved discrepancies, which finding 33 showed are
   powerful regime sensors on their own.
4. A cost metric for the watchdog (recommended; finding 31).
5. Authority: the max correction the system tolerates (safety property).

## Measured cost (finding 35)

| target | n | rate | CPU | RAM |
|---|---|---|---|---|
| desktop/ESP32-class (float) | 64-128 | any | 6-42 us/step | 6-8 KB |
| Cortex-M4 (Q15) | 64 | 1 kHz | ~6-10% | ~2 KB |
| Cortex-M0+ (Q15) | 64 | 100 Hz | 2-3% | ~2 KB |
| Arduino Uno / ATmega328 | 32-64 | 50-100 Hz | 7-17% | 0.8-1.5 KB |
| PIC18 | 32 | 50 Hz | ~10-20% | ~1 KB |
| dsPIC33 / PIC24 | 64 | 1 kHz | few % | ~2 KB |
| PIC16/ATtiny (heroic) | 16 | 10-20 Hz | high | <0.7 KB |

Not yet implemented, in value order: warm-started Golgi bisection (~2x),
Q15/LUT-tanh port (also fixes cross-device determinism, finding 29/32),
active-set loops, NLMS-only build (-1.5 KB, costs ~car-level accuracy),
fixed-capacity allocation.

## The deployment list

Control-mode (continuous FEL; the strong suit — findings 30-33):

1. **FOC motor drives — anticogging/ripple/friction** (beachhead market):
   learns torque residual vs rotor angle, current, temperature; teacher =
   the current loop's own correction. Replaces one-shot factory
   calibration; tracks wear. Limit: needs angle observable (encoder/hall).
2. **Ag autosteer / line-following rovers**: steering residual vs slope,
   implement load, terrain (the rover scenario is literally this).
   Limit: RTK/vision line reference must exist; brain trims, not guides.
3. **Gimbals / camera stabilization**: torque vs gimbal angle/rate —
   cable drag, imbalance per payload. Same code path as (1).
4. **CNC / 3D printers**: thermal drift vs temperature sensors (episodic
   teacher = probing residuals); resonance/backlash drift on top of
   static input shaping. Limit: slow thermal loops need hours of samples.
5. **HVAC / heat pumps**: EEV superheat trim, defrost timing, VAV loops
   vs time-of-day/outdoor-temp/compressor context. Ships inside a $3
   controller, offline. Limit: day-scale periodicity means days to learn.
6. **Drone/quadcopter trim**: hover-thrust and CG trim vs payload,
   battery sag, prop wear; gust feedforward from position-locked context.
7. **Prosthetics / exoskeletons**: per-user assist torque vs gait phase
   (the certification decomposition is the differentiator here).
8. **EV BMS thermal/charge trim**: derating-threshold trim vs SoC,
   ambient, age. Automotive cycles are slow; year-3 market.
9. **Satellite ADCS**: reaction-wheel friction vs speed/temperature,
   dipole residual vs orbital phase. Determinism is unusually valued.
10. **Boat/RC autopilots**: heading trim vs heel, load, current.
11. **Hard-disk-servo-class precision loops**: repeatable runout vs
    rotation phase (classical AFC's home turf — we generalize it to
    multi-context).
12. **Pick-and-place / pipetting robots**: settle-time residual vs move
    length, arm pose, payload.
13. **Conveyor/web tension, extruder pressure, wire feed**: residual vs
    speed, material, temperature.
14. **Crane anti-sway assist**: feedforward vs load mass and rope length
    (authority must stay low; operator is the outer loop).
15. **Solar trackers**: backlash and stiffness residual vs elevation.
16. **Wind turbine yaw/pitch micro-trim**: residual vs wind sector.
17. **Engine idle / small-genset governors**: load-anticipation trim vs
    accessory state.
18. **Thermal appliances** (espresso, sous-vide, reflow, 3D-printer
    beds): overshoot trim vs ambient, fill, cycle phase — the PIC16-class
    target.
19. **Elevator releveling / door profiles**: residual vs load and floor.
20. **Pool/greenhouse/irrigation dosing**: correction vs season/time
    context (low $/unit; easy wins).

Decision-mode (episodic residual scoring — competent but the weaker
regime; launcher 39%, fighter 93% ceiling):

21. **Adaptive game difficulty / NPC skill**: score actions by earned
    outcome; novelty exploration built in.
22. **Cache prefetch / speculative-work scoring**: per-decision residual
    on a cheap heuristic's estimate.
23. **Retry/timeout tuning in embedded firmware**: per-event aim
    correction on backoff heuristics.
24. **Shot/aim correction in launcher-class tools** (sprayers, seeders,
    ball machines): the launcher scenario verbatim.

Fleet variants of all of the above: shared frozen basis -> exact weight
merging (finding 22), instinct-file export planned; homogeneous fleets
merge both layers, heterogeneous slow-only. Precondition: identical basis
seed and no recycle drift (open debt).

## Standing limitations (the honest list)

- **The context boundary (findings 15, 33).** It learns only what its
  channels can see. Proprioception sees more than expected, but a
  regime invisible to every channel is averaged, not separated. Context
  design is where the domain knowledge lives — it is the ONE piece of
  real integration engineering.
- **Reflex timescale only.** No planning, no multi-step credit (finding
  21), no goal representation: the fighter's 93% ceiling and the maze
  are this boundary. The next-parts menu (docs/horizons) is the answer,
  not more cerebellum.
- **Decision mode is the weaker half**: sparse episodic outcomes learn
  slower and noisier than dense FEL streams (launcher/maze vs balance).
- **Delay**: FEL credit smears when actuation-to-teacher delay exceeds
  the eligibility window (~0.3 s); untested beyond small lags; filtered-x
  is the known fix if it bites (F5, docs/horizons failure modes).
- **The teacher must respond to the actuator** (PORTING.md rule 1) and
  should not contain the host's integrator term where DC ownership
  fights can arise (F6 — untested, watch for slow hunting).
- **Harm coverage**: the correlation gate + outcome watchdog cover
  "worse than without it" when a cost metric is wired; without one, only
  teacher-visible harm is caught. Anchor staleness under environment
  drift is a known gap (disengagement probes designed, not built).
- **Cross-device bit-determinism not yet true** (findings 29/32): same
  binary on same chip is deterministic; instinct-file portability awaits
  the software-tanh build.
- **Single-plant weight vector**: interference is defused by
  proprioceptive context in the cases tested, but a truly aliased
  multi-regime deployment (identical response, different required
  action) needs multiple models — not shipped.
- **No convergence proof for the full nonlinear loop** — the safety
  contract is enforced by gate/clamp/watchdog runtime assurance, not by
  proof of the learner. That is the design's honest certification story.
