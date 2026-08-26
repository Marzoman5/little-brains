# Roadmap: research rig -> publishable v1.0 library

Status 2026-08-26 late (see FINDINGS 36-37): Tier 1 items 1-4 are DONE —
`lib/littlebrains.js` (honored capacity, cfg-only seed, save/load as
caller-owned LB02 bytes, deterministic softmath "lb2") + `lib/c/lb.c`
verified BIT-IDENTICAL to JS via golden vectors (tools/gen_vectors.js,
lib/c/test_vectors.c; 12/12 smoke tests in tools/test_lib.js). Item 5
(persistence) is DONE as caller-owned bytes in both JS and C. The PIC16
integer build `lib/c/lb16.*` is EXPERIMENTAL: mechanisms verified, not
yet performing (finding 37 has the finish method). REMAINING before
v1.0: testbench imports the library + full lb2 board re-baseline (next
session's opener), then Tier 2 items 6-8 and Tier 3.

## Tier 1 — the library exists (est. 1-2 sessions)

1. Extract `lib/littlebrains.js`: single file, zero deps, NO window
   globals (seed via cfg only — BENCHSEED/SEEDK stay in the harness),
   capacity honored (cfg.n allocates n; growth optional up to cfg.cap),
   API: init(cfg) / step(ctx,teacher,learn) / read(ctx) / outcome(cost)
   / save(buf) / load(buf) / health(). Testbench imports it — one source
   of truth preserved.
2. C99 port `lib/c/brain.h` + `brain.c`: static allocation (LB_MAX_N
   compile-time or caller-supplied buffer), no malloc, no libm (LUT
   tanh), float and Q15 builds, ISR-callable (bounded WCET — the Golgi
   bisection is fixed-iteration by construction), optional CMSIS-DSP.
3. Golden vectors: JS reference emits (ctx,teacher)->(weights,out)
   sequences; C build must match bit-for-bit. Depends on:
4. Determinism batch (finding 29/32): software tanh (Cephes/rational),
   Box-Muller replacement, spec-exact subset only. Re-baseline the board
   once (numbers will shift), stamp a mathProfile version.

## Tier 2 — professional hygiene (est. 1 session)

5. Persistence: serialize slow weights + norm stats + basis fingerprint
   to a caller buffer (the instinct-file v1 header from V2-FEATURES);
   EEPROM/flash wear guidance. (Today NOTHING survives power cycles.)
6. Efficiency items: warm-start bisection (24->~4 iters), active-set
   loops, strip decision-mode arrays (cnt/energy) from control builds.
7. Library hygiene: argument validation + error codes, documented units
   and ranges, NaN policy, semver + CHANGELOG, LICENSE (owner decision:
   MIT or Apache-2.0).
8. CI: golden-vector tests + 10-seed board regression in GitHub Actions
   (tools/run_board.js is ready; determinism makes it machine-exact).

## Tier 3 — adoption surface (est. 1 session)

9. Examples: Arduino Uno sketch (thermal or line-follower), ESP32-IDF,
   STM32 HAL, plain-C sim harness; browser/Wokwi demo.
10. Packaging: Arduino Library Manager + PlatformIO registry + npm.
11. Docs: API reference (from PORTING.md), the safety-contract page,
    the certification kit skeleton (off=no-op statement, WCET table,
    memory bounds, IEC 61508 argument template), and a CONTEXT COOKBOOK
    — what to put in ctx per domain, incl. the finding-33 lesson that
    commanded-vs-achieved channels are regime sensors.
12. README with the variance-barred board table; repo layout split
    (lib/ vs research/).

Definition of done for v1.0: a stranger can `#include "brain.h"`, set
LB_MAX_N 16, add three lines to their loop, and get the documented
memory/WCET numbers — without reading any research file.
