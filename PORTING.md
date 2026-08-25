# Porting the brain — the simple version

The brain is one file with no dependencies beyond basic math. It has been
ported twice already (Python -> JavaScript, twice over) and the recipe below is
everything either port needed.

## What your system must have

1. **A loop.** Something that runs repeatedly — a control loop at 100 Hz, or
   one decision per event (a shot, a move). Both work.
2. **A number that says how it's going.** An error, a miss, a correction your
   existing controller had to make. The brain never sees the goal — only this.
3. **Things you can already observe.** Sensor readings, the controller's own
   output, where you are in a cycle. This becomes the context vector.

## The three-line integration

```python
u  = controller.step(...)            # whatever you already have
u += brain.step(ctx, teacher)        # the entire integration
```

- `ctx` — a plain array of raw observations. Do not scale or normalise them;
  the brain does that itself.
- `teacher` — the best signal is **your controller's own output** (`u` before
  the brain's addition). "Learn to do what my reflex keeps having to do."
  This is feedback-error learning and it removes the hardest problem
  (delayed credit) automatically. If there is no inner controller (episodic
  tasks like the launcher), use the outcome error converted to output units.
- The return value is the correction, in the same units as `u`.

## The three decisions you actually make

Everything else self-calibrates. These three are the engineering:

1. **Teacher must respond quickly to your actuator.** If your actuator changes
   the teacher signal within a step or two, learning is easy. If the effect
   arrives seconds later through integrations (steering -> heading -> position),
   pick a nearer signal (we used crosstrack+heading for the car until FEL made
   even that unnecessary).
2. **Context must contain what the correction depends on.** Position-locked
   effects need position channels (sin/cos of phase or track position, a few
   harmonics). State-dependent effects need the state. If the brain plateaus,
   the answer is usually a missing context channel, not a knob.
3. **Authority.** Tell the brain the maximum output your controller is allowed
   (`authority`). The brain clamps itself to half of that. This is a safety
   property of your system, not a tuning parameter.

## What you never tune

Learning rate (dimensionless, normalised), eligibility window, leak,
consolidation rate, sparsity, growth thresholds, the confidence gate — all
derived from the loop period `dt` and `authority`. The same constants ran a
double pendulum (100 Hz), a car (50 Hz), a plane (50 Hz), an artillery range
(one update per shot) and a maze solver without modification.

## Porting the module itself to a new language

It is ~150 lines. Port in this order and test each piece:

1. Seeded PRNG (mulberry32 + Box-Muller gaussian) — reproducible wiring.
2. Context normaliser: per-channel running mean/variance (EMA).
3. Granule layer: each unit reads 4 random context channels ->
   `tanh(w1*x1 + ... + w4*x4 + bias)`. Index array + weight array, no matrix.
4. Sparsity: continuous mode = Golgi leaky integrator; episodic mode
   (`dt >= 0.1 s`) = solve `inh = k * sum(relu(g - inh))` by bisection.
5. Readout: two weight vectors per output (fast + slow), output =
   `clip((wf+ws) . s)`, one-pole smoothing when `tau_out > dt`.
6. Learning: eligibility trace; NLMS update divided by trace power; rate
   scaled by familiarity (cosine of consecutive activity vectors); fast layer
   retains 0.995 per update; slow layer same update at 1/100 rate, no leak.
7. Competence gate: EMA correlation of prediction vs teacher -> alpha ramps
   on skill, holds on success, retreats only when teacher power returns
   toward its pre-brain level while skill is low.
8. Optional: replay buffer (episodic), growth + pruning (continuous only),
   NaN guard.

Numbers to expect (64 units, 12 context channels, browser JS, one core):
~3.6 µs per learning step, ~1 µs per read — 0.04% CPU at 100 Hz. Budget
~100x slower on a microcontroller: still under 5% of an ESP32 at 100 Hz.
Memory: (2 outputs) x (1024-unit cap) x 8 bytes x 2 layers ~ 33 KB, or fix
the cap at 128 units for ~4 KB.

## The safety contract (what "drop-in" means)

- Weights start at zero: the first output is exactly zero.
- Learning off = bit-exact no-op relative to your loop alone.
- Output is silent until predictions demonstrably match the teacher.
- If the brain ever makes things worse than before it engaged, its own
  watchdog withdraws it.
- Output never exceeds half your controller's authority.
- A NaN anywhere zeroes the fast layer and logs, instead of propagating.
