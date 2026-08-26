/* =====================================================================
 * lb16 — Little Brains for 8-bit-class MCUs (PIC16F1/PIC18/ATmega).
 *
 * STATUS: EXPERIMENTAL, NOT YET PERFORMING (2026-08-26). All mechanisms
 * are implemented and mechanically verified (472+30 B RAM, basis codes
 * differentiate, watchdog caps, save/load round-trips, AGC normalizer
 * converges) but the closed FEL loop underperforms the float reference
 * (rms ~0.24-0.40 vs 0.076) and the gate bootstrap can deadlock via
 * DC-integration runaway. Finish plan (FINDINGS 37): stage-wise golden
 * vectors from lib/c/lb.c — verify basis codes, then eligibility, then
 * the NLMS delta, then gate rho, each against the float reference,
 * instead of tuning constants blind. Use lib/c/lb.c (verified
 * bit-identical to JS) for M0+-class targets today.
 * Integer-only (Q15 fast path, Q30 slow layer), no float, no division
 * in the per-weight loop, no recursion, no malloc. C99 subset chosen to
 * compile under XC8/avr-gcc. NOT bit-compatible with the double build —
 * it is the same algorithm quantized (documented mathProfile "lb16").
 *
 * Deliberate reductions vs the full library (each is a measured
 * trade, see docs/APPLICATIONS.md):
 *   - familiarity-NLMS rule only (finding 26: Autostep's edge is small)
 *   - no oscillators / replay / growth / mix (fixed n)
 *   - caller scales ctx to Q15 in [-1, 1) — "scale your sensors once"
 *     replaces the running normalizer (the one integration knob here)
 *   - competence gate + outcome watchdog kept IN FULL: the safety
 *     contract is the part that never gets cut.
 *
 * RAM for n=16, nCtx<=8, 1 output: ~460 bytes. Flash: ~130-byte tanh
 * LUT + code. Estimated 100 Hz loop on a PIC16F1 @ 32 MHz: ~10% CPU.
 *
 *   int16_t u_pid = pid();                 // your loop, Q15 units
 *   lb16_step(&b, ctx_q15, u_pid, 1);
 *   int16_t u = sat_add(u_pid, b.corr);    // the 3-line integration
 * ===================================================================== */
#ifndef LB16_H
#define LB16_H
#include <stdint.h>

#ifndef LB16_N
#define LB16_N 16          /* units — compile-time, so RAM is static    */
#endif
#ifndef LB16_NCTX
#define LB16_NCTX 6        /* context channels                          */
#endif
#define LB16_FAN 4

typedef struct {
  /* wiring (flash-able on Harvard parts if seeded offline) */
  uint8_t idx[LB16_N*LB16_FAN];      /* context index per tap           */
  int16_t w_in[LB16_N*LB16_FAN];     /* Q15 input weights               */
  int16_t bias[LB16_N];              /* Q12.3-ish pre-activation bias   */
  /* block-floating-point normalizer: feed RAW sensor ints of any
     consistent scale; each channel is centered by an EMA mean and
     scaled by a power-of-2 shift so its typical swing maps to ~1 sigma
     internally. No division, no sqrt — PIC16-friendly "no knobs". */
  int16_t nrm_mu[LB16_NCTX];         /* running mean (raw units)        */
  int32_t nrm_amp16[LB16_NCTX];      /* running mean |x-mu|, x16 (the
                                        x16 keeps the EMA from stalling
                                        below its own LSB)              */
  int8_t  nrm_sh[LB16_NCTX];         /* power-of-2 gain exponent        */
  /* learned state */
  int16_t wf[LB16_N];                /* fast weights, Q12 (+-8 range)   */
  int32_t ws[LB16_N];                /* slow weights, Q27 (+-16 range)  */
  int16_t elig[LB16_N];              /* eligibility trace, Q12          */
  int16_t s_act[LB16_N];             /* sparse code, Q15                */
  int16_t prev_s[LB16_N];            /* for familiarity                 */
  /* scalars */
  uint16_t rng;                      /* 16-bit xorshift for seeding     */
  int16_t td_q15;                    /* elig decay per step             */
  int16_t leak_q15;                  /* fast-layer retention            */
  int16_t clamp_q12;                 /* output clamp (authority/2), Q12 */
  int16_t wbox_q12;                  /* weight projection bound, Q12    */
  int16_t out_q12;                   /* smoothed output                 */
  int16_t corr_q12;                  /* gated correction — ADD THIS     */
  int16_t tau_mix_q15;               /* output smoothing coefficient    */
  /* gate (Q15 EMAs; rho test done squared — no sqrt anywhere) */
  int32_t g_ru, g_rr, g_uu, uu0;
  uint8_t uu0_set;
  uint16_t alpha_q15;                /* authority ramp 0..32767         */
  uint16_t gate_up_q15, gate_dn_q15; /* per-step ramp constants         */
  uint16_t step_lo;                  /* step counter (wraps, ok)        */
  uint16_t warm_steps;               /* steps before uu0 anchors        */
  /* outcome watchdog (integer costs, caller units) */
  uint16_t o_pre, o_now;             /* cost EMAs                       */
  uint8_t o_pre_n, o_n, o_started;
  uint16_t o_cap_q15;                /* authority cap from outcomes     */
} Lb16;

/* seed the frozen basis + all constants. dt_ms: loop period in ms.
   authority_q12: your controller's max output in Q12 (brain clamps to
   half). Call once at boot (or bake the wiring into flash offline). */
void lb16_init(Lb16 *b, uint16_t seed, uint16_t dt_ms, int16_t authority_q12);

/* one step. ctx: LB16_NCTX channels of RAW sensor ints, any consistent
   per-channel scale — the built-in block-floating normalizer centers
   and ranges them (no knobs). teacher: your controller's own output in
   Q12 (FEL). learn: 0 = read. After the call, b->corr_q12 is the
   correction to ADD (already gated, clamped, watchdog-capped). */
void lb16_step(Lb16 *b, const int16_t *ctx, int16_t teacher_q12, uint8_t learn);

/* harm watchdog: report the cost you already measure (uint16, lower is
   better, any consistent unit), e.g. once per cycle/episode. */
void lb16_outcome(Lb16 *b, uint16_t cost);

/* persistence, caller-owned: fills buf with a versioned blob (slow
   weights + gate anchor; LB16 blob, ~4+2+N*4+4 bytes). Returns bytes
   written, 0 if buf too small. Caller writes it to EEPROM/flash.
   lb16_load returns 0 ok / negative error (wrong seed/size/crc). */
uint16_t lb16_save(const Lb16 *b, uint8_t *buf, uint16_t buf_len);
int8_t lb16_load(Lb16 *b, const uint8_t *buf, uint16_t len);

#endif
