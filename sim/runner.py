"""Closed-loop runner: 100 Hz control over 1 kHz physics, optional
cerebellar module. The module integration is exactly one line."""

import numpy as np

DT_SIM = 0.001
DT_CTRL = 0.01
SUBSTEPS = round(DT_CTRL / DT_SIM)

N_CTX = 12  # [th1,th2,w1,w2,e1,e2,ed1,ed2,u1,u2,sin,cos]


def make_ctx(plant, err, err_dot, u_pid, t, clock_freq):
    ph = 2 * np.pi * clock_freq * t
    return np.concatenate([
        plant.theta, plant.omega, err, err_dot, u_pid,
        [np.sin(ph), np.cos(ph)],
    ])


def run_loop(plant, pid, ref, cereb=None, t_end=60.0,
             clock_freq=None, callbacks=None, noise_std=0.0, noise_seed=0,
             teacher="err"):
    """callbacks: list of (t_trigger, fn) applied once when t >= trigger.
    noise_std: gaussian sensor noise (rad, rad/s) on measured theta/omega.
    teacher: "err" trains the module on the tracking error (decorrelation);
    "u_fb" trains it on the PID output (feedback-error learning)."""
    n = round(t_end / DT_CTRL)
    log = {k: np.zeros((n, 2)) for k in ("theta", "ref", "err", "err_meas",
                                         "u_pid", "u_cereb")}
    log["t"] = np.zeros(n)
    log["fast_norm"] = np.zeros(n)
    log["slow_norm"] = np.zeros(n)
    log["n_basis"] = np.zeros(n)
    log["sparsity"] = np.zeros(n)
    pending = sorted(callbacks or [], key=lambda c: c[0])
    nrng = np.random.default_rng(noise_seed)

    for i in range(n):
        t = plant.t
        while pending and t >= pending[0][0]:
            pending.pop(0)[1]()

        theta_m, omega_m = plant.theta, plant.omega
        if noise_std > 0.0:
            theta_m = theta_m + nrng.normal(0, noise_std, 2)
            omega_m = omega_m + nrng.normal(0, noise_std, 2)

        r, rdot = ref.theta(t), ref.theta_dot(t)
        err = r - theta_m
        err_dot = rdot - omega_m

        u = pid.step(err, err_dot, DT_CTRL)
        log["u_pid"][i] = u
        if cereb is not None:
            f = clock_freq if clock_freq is not None else plant.dist_freq
            ph = 2 * np.pi * f * t
            ctx = np.concatenate([theta_m, omega_m, err, err_dot, u,
                                  [np.sin(ph), np.cos(ph)]])
            signal = u if teacher == "u_fb" else err
            u = u + cereb.step(ctx, signal)   # the one-line integration
            log["u_cereb"][i] = u - log["u_pid"][i]
            log["fast_norm"][i] = cereb.fast_norm()
            log["slow_norm"][i] = cereb.slow_norm()
            log["n_basis"][i] = cereb.n_basis
            log["sparsity"][i] = cereb.sparsity()

        log["t"][i] = t
        log["theta"][i] = plant.theta
        log["ref"][i] = r
        log["err"][i] = r - plant.theta   # true tracking error
        log["err_meas"][i] = err

        for _ in range(SUBSTEPS):
            plant.step(u, DT_SIM)
    return log


def rms(log, t_from, t_to=np.inf):
    m = (log["t"] >= t_from) & (log["t"] < t_to)
    return float(np.sqrt((log["err"][m] ** 2).sum(axis=1).mean()))


def rolling_rms(err, window_samples):
    sq = (err ** 2).sum(axis=1)
    kernel = np.ones(window_samples) / window_samples
    mean_sq = np.convolve(sq, kernel, mode="full")[:len(sq)]
    counts = np.minimum(np.arange(1, len(sq) + 1), window_samples)
    mean_sq[:window_samples] = np.cumsum(sq)[:window_samples] / counts[:window_samples]
    return np.sqrt(mean_sq)
