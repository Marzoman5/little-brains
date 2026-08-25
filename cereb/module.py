"""Cerebellar adaptive correction module.

A linear readout over a frozen sparse random nonlinear basis, trained by
a decorrelation rule with a leaky eligibility trace. Goal-agnostic: it
sees a context vector and a scalar (or vector) error, never a setpoint.

Integration is one line:  u = pid.step(...); u += cereb.step(ctx, err)

With lr_fast = 0 the returned correction is bit-exact zero (weights
initialise to zero and never move), so the host loop is untouched.

Layers:
  fast  — cortical, volatile: decorrelation on the error, leaky.
  slow  — nuclear, stable: copies the fast layer (consolidation).
Growth controller: adds basis capacity only when the fast weights have
converged AND the residual error is still autocorrelated (structured).
"""

import numpy as np

FAN_IN = 4  # mossy-fibre inputs per granule cell


class Cerebellum:
    def __init__(self, n_ctx, n_out, dt,
                 n_basis=1000,
                 lr_fast=0.5,
                 trace_decay=0.95,
                 leak=1e-5,
                 clamp=12.5,
                 k_golgi=0.3,
                 tau_golgi=0.1,
                 tau_out=0.05,
                 norm_alpha=2e-3,
                 consolidation=False,
                 lr_slow=0.05,            # two-rate model; pair consolidation
                                          # with a leaky fast layer (~5e-3)
                 growth=False,
                 growth_block=0.2,        # grow by 20% of current size
                 growth_cap=4000,
                 growth_cooldown=20.0,    # s
                 conv_thresh=0.01,        # rel. |w| change over 10 s
                 autocorr_thresh=0.15,    # max |rho| lags 1..5 @ 10 Hz
                 rms_floor=0.01,          # rad; below this, never grow
                 prune_frac=0.02,         # of mean |w| counts as dead
                 prune_after=60.0,        # s dead before recycling
                 nlms=False,              # normalise updates by elig power:
                                          # lr becomes dimensionless (0..2)
                 gate=False,              # competence gate: output scaled by
                                          # alpha(corr(prediction, teacher));
                                          # learn silently, act when proven
                 gate_tau=5.0,            # s, correlation EMA
                 seed=0):
        self.n_ctx, self.n_out, self.dt = n_ctx, n_out, dt
        self.lr_fast = lr_fast
        self.trace_decay = trace_decay
        self.leak = leak
        self.clamp = clamp
        self.k_golgi = k_golgi
        self.tau_golgi = tau_golgi
        self.tau_out = tau_out
        self.norm_alpha = norm_alpha
        self.consolidation = consolidation
        self.lr_slow = lr_slow
        self.growth = growth
        self.growth_block = growth_block
        self.growth_cap = growth_cap
        self.growth_cooldown = growth_cooldown
        self.conv_thresh = conv_thresh
        self.autocorr_thresh = autocorr_thresh
        self.rms_floor = rms_floor
        self.prune_frac = prune_frac
        self.prune_after = prune_after
        self.nlms = nlms
        self.gate = gate
        self.gate_tau = gate_tau
        # episodic mode: updates far slower than tau_golgi (one per shot /
        # decision) — exact sparsity solve, replay rehearsal, growth off
        self.episodic = dt >= tau_golgi
        self._rb_cap = 64
        self._rb_n = 0
        self._rb_i = 0
        self._rb_ctx = np.zeros((self._rb_cap, n_ctx))
        self._rb_tot = np.zeros((self._rb_cap, n_out))
        self._prev_s = np.zeros(n_basis)
        # competence-gate statistics (per output channel)
        self._g_ru = np.zeros(n_out)
        self._g_rr = np.zeros(n_out)
        self._g_uu = np.zeros(n_out)
        self._g_uu0 = None                      # teacher power before engaging
        self.alpha = np.zeros(n_out) if gate else np.ones(n_out)

        self._rng = np.random.default_rng(seed)
        n = n_basis
        self.idx = self._rng.integers(0, n_ctx, size=(n, FAN_IN))
        self.w_in = self._rng.normal(0, 1 / np.sqrt(FAN_IN), size=(n, FAN_IN))
        self.bias = self._rng.normal(0, 0.5, size=n)

        # running per-channel normalisation
        self.mu = np.zeros(n_ctx)
        self.var = np.ones(n_ctx)

        # Golgi inhibition state (scalar, shared)
        self.inh = 0.0

        # plastic state — all zeros: provable no-op at t=0
        self.w_fast = np.zeros((n_out, n))
        self.w_slow = np.zeros((n_out, n))
        self.elig = np.zeros(n)

        self.out = np.zeros(n_out)  # filtered output state
        self.nan_events = 0
        self.s = np.zeros(n)        # last activity, exposed for telemetry

        # growth/prune bookkeeping
        self.t = 0.0
        self._step_i = 0
        self._dec_acc = np.zeros(n_out)   # 10 Hz decimation accumulator
        self._dec_n = 0
        self._err_hist = []               # decimated error rows, <= 300
        self._norm_hist = []              # |w_fast| once per second, <= 11
        self._low_time = np.zeros(n)      # s each unit has been ~zero-weight
        self._last_growth = -1e9
        self.events = []                  # (t, str) growth/prune/nan log

    @property
    def n_basis(self):
        return self.bias.shape[0]

    # ------------------------------------------------------------------
    def step(self, ctx, err):
        ctx = np.asarray(ctx, dtype=float)
        err = np.atleast_1d(np.asarray(err, dtype=float))
        learning = self.lr_fast != 0.0

        # per-channel running normalisation (mandatory: without it the
        # wide-range channels own the random projection)
        a = self.norm_alpha
        self.mu += a * (ctx - self.mu)
        self.var += a * ((ctx - self.mu) ** 2 - self.var)
        x = (ctx - self.mu) / np.sqrt(self.var + 1e-8)

        # granule layer: 4 multiplies per unit, saturating nonlinearity
        z = (self.w_in * x[self.idx]).sum(axis=1) + self.bias
        g = np.tanh(z)

        # Golgi sparsification. The relaxation loop is numerically unstable
        # when updates are sparse (dt >= tau), so episodic mode solves the
        # fixed point inh = k * sum(relu(g - inh)) exactly instead.
        if self.episodic:
            self.inh = self._solve_inh(g)
            s = np.maximum(g - self.inh, 0.0)
        else:
            s = np.maximum(g - self.inh, 0.0)
            self.inh += ((s.sum() * self.k_golgi - self.inh)
                         * self.dt / self.tau_golgi)
        self.s = s

        raw = np.clip((self.w_fast + self.w_slow) @ s, -self.clamp, self.clamp)
        if self.tau_out > self.dt:
            self.out += (raw - self.out) * (self.dt / self.tau_out)
        else:
            self.out = raw

        if self.gate and learning:
            # Competence gate, ratchet-with-watchdog form.
            # Ramp up only while the module demonstrably predicts the teacher
            # (skill rho); once earned, alpha holds — success shrinks the
            # teacher to noise, which must not read as lost skill. Retreat
            # only on harm (teacher power above its achieved best) WITHOUT
            # skill — a plant change raises teacher power too, but then the
            # module is re-learning (rho high) and must stay engaged.
            a = min(0.15, self.dt / self.gate_tau)
            self._g_ru += a * (raw * err - self._g_ru)
            self._g_rr += a * (raw * raw - self._g_rr)
            self._g_uu += a * (err * err - self._g_uu)
            rho = self._g_ru / np.sqrt(self._g_rr * self._g_uu + 1e-12)
            if self._g_uu0 is None and self.t > 2 * self.gate_tau:
                self._g_uu0 = self._g_uu.copy()   # the "without a brain" level
            if self._g_uu0 is None:
                harm = np.zeros(self.n_out, dtype=bool)
            else:
                harm = (self._g_uu > 0.7 * self._g_uu0) & (rho < 0.3)
            skill = np.clip((rho - 0.15) / 0.45, 0.0, 1.0)
            self.alpha = np.where(
                harm,
                self.alpha * max(0.0, 1.0 - self.dt / 2.0),
                np.minimum(1.0, self.alpha + (self.dt / 8.0) * skill))

        if learning:
            self.elig = self.elig * self.trace_decay + s
            if self.nlms:
                # familiarity-scaled NLMS: correlated (continuous) context
                # streams learn at full rate; novel/jumpy (episodic) contexts
                # learn gently to average out cross-sample interference
                num = float(s @ self._prev_s)
                den = np.sqrt(float(s @ s) * float(self._prev_s @ self._prev_s)
                              + 1e-12)
                fam = min(1.0, max(0.0, num / den))
                rate = 0.2 + 0.8 * fam
                self._prev_s = s.copy()
                step = rate * self.lr_fast / ((self.elig @ self.elig) + 1e-8)
            else:
                step = self.lr_fast
            self.w_fast += step * np.outer(err, self.elig)
            self.w_fast *= (1.0 - self.leak)
            if self.consolidation:
                # two-rate consolidation (Smith et al. 2006): the slow layer
                # learns from the same error at ~1/100 the rate, with no
                # leak. Washout barely touches it because the fast layer
                # cancels the error before the slow layer can unlearn.
                sstep = (self.lr_slow / ((self.elig @ self.elig) + 1e-8)
                         if self.nlms else self.lr_slow)
                self.w_slow += sstep * np.outer(err, self.elig)

            if not (np.all(np.isfinite(self.w_fast))
                    and np.all(np.isfinite(self.w_slow))):
                self.w_fast[:] = 0.0
                self.elig[:] = 0.0
                if not np.all(np.isfinite(self.w_slow)):
                    self.w_slow[:] = 0.0
                self.nan_events += 1
                self.events.append((self.t, "NaN guard: fast layer zeroed"))

            if self.episodic:
                i = self._rb_i
                self._rb_ctx[i] = ctx
                self._rb_tot[i] = self.alpha * self.out + err
                self._rb_i = (i + 1) % self._rb_cap
                self._rb_n = min(self._rb_n + 1, self._rb_cap)
                self._replay()

            self._housekeeping(err)

        self.t += self.dt
        self._step_i += 1
        return self.alpha * self.out if self.gate else self.out.copy()

    # ------------------------------------------------------------------
    def _solve_inh(self, g):
        """Exact Golgi fixed point inh = k * sum(relu(g - inh)), bisection."""
        lo, hi = 0.0, 1.0
        for _ in range(24):
            mid = 0.5 * (lo + hi)
            ssum = np.maximum(g - mid, 0.0).sum()
            if ssum * self.k_golgi > mid:
                lo = mid
            else:
                hi = mid
        return 0.5 * (lo + hi)

    def _forward(self, ctx):
        """Side-effect-free forward pass with current stats (for replay)."""
        x = (ctx - self.mu) / np.sqrt(self.var + 1e-8)
        g = np.tanh((self.w_in * x[self.idx]).sum(axis=1) + self.bias)
        return np.maximum(g - self._solve_inh(g), 0.0)

    def _replay(self):
        """Episodic rehearsal: LMS toward the stored TOTAL demand (applied +
        residual at storage time) — a fixed target, so it converges."""
        if self._rb_n < 8:
            return
        for _ in range(4):
            pick = int(self._rng.integers(0, self._rb_n))
            s = self._forward(self._rb_ctx[pick])
            ep2 = float(s @ s) + 1e-8
            pred = (self.w_fast + self.w_slow) @ s
            self.w_fast += np.outer(
                0.2 * (self._rb_tot[pick] - pred) / ep2, s)

    def _housekeeping(self, err):
        """Growth trigger bookkeeping + once-a-second checks."""
        # decimate error to 10 Hz for the autocorrelation test
        self._dec_acc += err
        self._dec_n += 1
        if self._dec_n >= max(1, round(0.1 / self.dt)):
            self._err_hist.append(self._dec_acc / self._dec_n)
            if len(self._err_hist) > 300:          # 30 s window
                self._err_hist.pop(0)
            self._dec_acc = np.zeros(self.n_out)
            self._dec_n = 0

        if self._step_i % max(1, round(1.0 / self.dt)) != 0:
            return

        # once per second -------------------------------------------------
        self._norm_hist.append(self.fast_norm())
        if len(self._norm_hist) > 11:
            self._norm_hist.pop(0)

        if self.growth:
            self._maybe_prune()
            self._maybe_grow()

    def _residual_stats(self):
        """(rms, max |autocorr| over lags 1..5) of the decimated residual."""
        if len(self._err_hist) < 100:
            return 0.0, 0.0
        e = np.asarray(self._err_hist)
        rms = float(np.sqrt((e ** 2).sum(axis=1).mean()))
        rho_max = 0.0
        for j in range(e.shape[1]):
            x = e[:, j] - e[:, j].mean()
            denom = (x * x).sum() + 1e-12
            for lag in range(1, 6):
                rho = abs((x[:-lag] * x[lag:]).sum() / denom)
                rho_max = max(rho_max, float(rho))
        return rms, rho_max

    def _converged(self):
        """Fast weights stopped changing: <conv_thresh relative norm
        change over the last 10 s."""
        if len(self._norm_hist) < 11:
            return False
        now, ago = self._norm_hist[-1], self._norm_hist[0]
        return abs(now - ago) < self.conv_thresh * max(now, 1.0)

    def _maybe_grow(self):
        # growth needs sample volumes episodic regimes don't have: the
        # whiteness test is unreliable under ~1000 samples
        if self.episodic:
            return
        if self.n_basis >= self.growth_cap:
            return
        if self.t - self._last_growth < self.growth_cooldown:
            return
        if not self._converged():
            return
        rms, rho = self._residual_stats()
        if rms < self.rms_floor or rho < self.autocorr_thresh:
            return
        n_new = min(max(int(self.n_basis * self.growth_block), 4),
                    self.growth_cap - self.n_basis)
        self._add_units(n_new)
        self._last_growth = self.t
        self.events.append(
            (self.t, f"grew {self.n_basis - n_new} -> {self.n_basis} "
                     f"(residual rms {rms:.4f}, autocorr {rho:.2f} > "
                     f"{self.autocorr_thresh})"))

    def _add_units(self, n_new):
        """New units come in with zero output weights: no transient."""
        r = self._rng
        self.idx = np.vstack([self.idx,
                              r.integers(0, self.n_ctx, size=(n_new, FAN_IN))])
        self.w_in = np.vstack([self.w_in,
                               r.normal(0, 1 / np.sqrt(FAN_IN),
                                        size=(n_new, FAN_IN))])
        self.bias = np.concatenate([self.bias, r.normal(0, 0.5, size=n_new)])
        z = np.zeros((self.n_out, n_new))
        self.w_fast = np.hstack([self.w_fast, z])
        self.w_slow = np.hstack([self.w_slow, z.copy()])
        self.elig = np.concatenate([self.elig, np.zeros(n_new)])
        self.s = np.concatenate([self.s, np.zeros(n_new)])
        self._prev_s = np.concatenate([self._prev_s, np.zeros(n_new)])
        self._low_time = np.concatenate([self._low_time, np.zeros(n_new)])

    def _maybe_prune(self):
        """Units whose combined weight stays ~zero for prune_after seconds
        get their input sampling re-randomised (recycled, not removed)."""
        w = np.abs(self.w_fast).sum(axis=0) + np.abs(self.w_slow).sum(axis=0)
        mean_w = w.mean()
        if mean_w < 1e-9:
            return
        low = w < self.prune_frac * mean_w
        self._low_time[low] += 1.0
        self._low_time[~low] = 0.0
        dead = np.flatnonzero(self._low_time > self.prune_after)
        if dead.size == 0:
            return
        r = self._rng
        self.idx[dead] = r.integers(0, self.n_ctx, size=(dead.size, FAN_IN))
        self.w_in[dead] = r.normal(0, 1 / np.sqrt(FAN_IN),
                                   size=(dead.size, FAN_IN))
        self.bias[dead] = r.normal(0, 0.5, size=dead.size)
        self.w_fast[:, dead] = 0.0
        self.w_slow[:, dead] = 0.0
        self.elig[dead] = 0.0
        self._low_time[dead] = 0.0
        self.events.append((self.t, f"recycled {dead.size} dead units"))

    # telemetry helpers -------------------------------------------------
    def sparsity(self):
        return float((self.s > 0).mean())

    def fast_norm(self):
        return float(np.linalg.norm(self.w_fast))

    def slow_norm(self):
        return float(np.linalg.norm(self.w_slow))
