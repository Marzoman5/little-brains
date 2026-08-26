/* =====================================================================
 * Little Brains — C99 port of lib/littlebrains.js (mathProfile "lb2").
 *
 * Bit-identical to the JS reference when compiled with strict IEEE-754
 * double semantics:   -O2 -ffp-contract=off   (no -ffast-math, ever).
 * Verified by golden vectors (tools/gen_vectors.js + test_vectors.c).
 *
 * No malloc, no libm beyond sqrt(), no globals, reentrant. The caller
 * provides one memory arena sized by lb_mem_required(); on an MCU that
 * is a static byte array. Persistence is caller-owned bytes: lb_save()
 * fills a buffer (same LB02 blob as the JS library — a brain saved in
 * a browser loads on a microcontroller and vice versa, little-endian),
 * and the caller writes it to EEPROM/flash/disk however it likes.
 *
 *   double mem[LB_MEM_DOUBLES];             // or arena from lb_mem_required
 *   LbBrain b; LbConfig c = {.n_ctx=6,.n_out=1,.dt=0.02f,.authority=1,
 *                            .n=16,.seed=3};
 *   lb_init(&b,&c,mem,sizeof mem);
 *   ...
 *   u  = pid_step(err);
 *   u += lb_step(&b, ctx, &u_pid, 1)[0];    // the 3-line integration
 * ===================================================================== */
#ifndef LITTLEBRAINS_H
#define LITTLEBRAINS_H

#include <stdint.h>
#include <stddef.h>

#define LB_VERSION      "0.9.0"
#define LB_MATH_PROFILE 2      /* "lb2" */
#define LB_FAN          4

typedef struct {
  int    n_ctx;        /* required: context channels                     */
  int    n_out;        /* required: outputs                              */
  double dt;           /* required: loop period, seconds                 */
  double authority;    /* required: host's max output (brain uses half)  */
  int    n;            /* units, default 64                              */
  int    cap;          /* growth ceiling; default n (growth off)         */
  int    seed;         /* basis seed, default 1                          */
  int    rule;         /* 0 auto (episodic->NLMS), 1 NLMS, 2 Autostep    */
  int    gate;         /* 0 = on (default), -1 = off                     */
  int    osc;          /* 0 = auto (3 continuous), -1 = none, >0 count   */
  int    rb_cap;       /* 0 = auto (256 episodic), -1 = none, >0 size    */
  int    sparsifier;   /* 0 golgi (default), 1 kwta                      */
  double tau_elig;     /* default 0.3 s                                  */
  double leak;         /* default 0.9995 per update                      */
  double tau_out;      /* default max(0.05, 2.5*dt)                      */
  double k_frac;       /* kwta only; default regime-keyed                */
  double alpha0;       /* Autostep init step; default 0.05               */
  int    mix;          /* fast/slow convex mix, default 0 (finding 27)   */
  int    mix_pc;       /* per-component-error mix variant, default 0     */
} LbConfig;

typedef struct {
  /* wiring/config (fixed after init) */
  int n_ctx_in, n_out, n0, cap, n, n_ctx, n_osc, seed;
  int rule_nlms, gate_on, sparsifier, rb_cap, mix_on, mix_pc, bound_es;
  double dt, clamp, wbox, td, leak_f, mu_s, a_norm, tau_out, gate_tau;
  double meta_theta, meta_tau, alpha0, log_alpha0, mu_f, k_frac;
  double tau_golgi, osc_k;
  int episodic, kwta;
  uint32_t rng;
  /* arena-carved state (see lb.c for layout) */
  int32_t *idx;                       /* cap*FAN                         */
  double *w_in, *bias;                /* cap*FAN, cap                    */
  double *mu, *varr, *xn, *xn_tmp;    /* n_ctx each                      */
  double *wf, *ws, *beta, *h_tr, *v_n, *energy, *cnt;  /* n_out*cap each */
  double *mix_a, *mix_denom, *lam_j, *diff_j;          /* n_out each     */
  double *prev_s, *elig, *s_act, *s_tmp, *s_tmp2, *low_time; /* cap each */
  double *out, *raw, *corr;           /* n_out each                      */
  double *g_ru, *g_rr, *g_uu, *uu0, *alpha, *dec_acc;  /* n_out each     */
  double *osc_ph, *osc_w;             /* n_osc each                      */
  double *rb_ctx, *rb_tot;            /* rb_cap*n_ctx_in, rb_cap*n_out   */
  uint8_t *rb_mask;                   /* rb_cap*n_out                    */
  double *wc_ctx;                     /* 8*n_ctx                         */
  double *err_hist, *norm_hist;       /* growth only: 300*n_out, 11      */
  double *top_buf;                    /* kwta only                       */
  /* scalars */
  int uu0_set;
  double t; long step_i; double active; int nan_events;
  double mass, t_mag;
  int dec_n, dec_every, check_every;
  int err_n, err_i, norm_n;
  double last_growth;
  int rb_n, rb_i, wc_n, wc_i;
  double o_pre, o_now; int o_pre_n, o_n, o_pre_set, o_now_set;
  double o_cap, harm_ratio;
  double fam; long fam_step;
  int grow_enabled;
} LbBrain;

/* bytes of arena needed for a config (0 on invalid config) */
size_t lb_mem_required(const LbConfig *cfg);

/* 0 on success; -1 invalid config; -2 arena too small */
int lb_init(LbBrain *b, const LbConfig *cfg, void *mem, size_t mem_len);

/* one step. ctx: n_ctx doubles. teacher: n_out doubles (the host's own
   output for FEL, or outcome residuals). learn: 0 = pure read.
   Returns b->corr (n_out doubles): the correction to ADD. */
const double *lb_step(LbBrain *b, const double *ctx, const double *teacher,
                      int learn);
const double *lb_read(LbBrain *b, const double *ctx);

/* harm watchdog: report the cost metric you already have (>=0, lower is
   better), e.g. once per episode/cycle. See FINDINGS.md 31. */
void lb_outcome(LbBrain *b, double cost);

void lb_reset(LbBrain *b);
double lb_mean_alpha(const LbBrain *b);

typedef struct { double wf, ws, clamp_frac, o_cap; } LbHealth;
LbHealth lb_health(const LbBrain *b);

/* persistence: caller-owned bytes (LB02 blob, cross-loads with the JS
   library). lb_save returns bytes written (0 if buf too small; call
   lb_save_size first). lb_load returns 0 ok / negative error. */
size_t lb_save_size(const LbBrain *b, int include_fast);
size_t lb_save(const LbBrain *b, uint8_t *buf, size_t buf_len,
               int include_fast);
int lb_load(LbBrain *b, const uint8_t *buf, size_t len);

/* fleet: precision-weighted slow-layer merge (shared basis required) */
void lb_merge_from(LbBrain *b, const LbBrain *other);

/* the deterministic softmath, exposed for tests */
double lb_soft_tanh(double x);
double lb_soft_exp(double x);
double lb_soft_log(double x);
double lb_soft_sin(double x);   /* arg already in [-pi,pi] */
double lb_soft_cos(double x);
double lb_wrap_pi(double x);
uint32_t lb_crc32(const uint8_t *p, size_t n);

#endif
