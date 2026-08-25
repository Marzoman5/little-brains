"""Stage 1: plant + PID baseline. Runs the tracking task and produces
an RMS tracking-error plot. No cerebellar module anywhere in this file."""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from sim import DoublePendulum, PID, Reference

DT_SIM = 0.001          # 1 kHz physics
DT_CTRL = 0.01          # 100 Hz control
SUBSTEPS = round(DT_CTRL / DT_SIM)
T_END = 60.0
SETTLE = 5.0            # excluded from the headline RMS number
RMS_WINDOW = 5.0        # seconds, rolling


def run(gains, dist_amp=0.0, dist_freq=1.0, seed_state=None):
    plant = DoublePendulum()
    plant.dist_amp = np.array([dist_amp, dist_amp])
    plant.dist_freq = dist_freq
    ref = Reference()
    pid = PID(*gains)

    plant.reset(*ref.theta(0.0))
    n = round(T_END / DT_CTRL)
    log = {k: np.zeros((n, 2)) for k in ("theta", "ref", "err", "u")}
    log["t"] = np.zeros(n)

    for i in range(n):
        t = plant.t
        r, rdot = ref.theta(t), ref.theta_dot(t)
        err = r - plant.theta
        err_dot = rdot - plant.omega
        u = pid.step(err, err_dot, DT_CTRL)

        log["t"][i] = t
        log["theta"][i] = plant.theta
        log["ref"][i] = r
        log["err"][i] = err
        log["u"][i] = u

        for _ in range(SUBSTEPS):
            plant.step(u, DT_SIM)
    return log


def rolling_rms(err, window_samples):
    """RMS of the combined joint error over a trailing window."""
    sq = (err ** 2).sum(axis=1)
    kernel = np.ones(window_samples) / window_samples
    mean_sq = np.convolve(sq, kernel, mode="full")[:len(sq)]
    # early samples average over fewer points
    counts = np.minimum(np.arange(1, len(sq) + 1), window_samples)
    mean_sq[:window_samples] = np.cumsum(sq)[:window_samples] / counts[:window_samples]
    return np.sqrt(mean_sq)


def main():
    gains = ([120.0, 60.0], [40.0, 20.0], [18.0, 7.0])
    log = run(gains)

    mask = log["t"] >= SETTLE
    overall = np.sqrt((log["err"][mask] ** 2).sum(axis=1).mean())
    per_joint = np.sqrt((log["err"][mask] ** 2).mean(axis=0))
    print(f"overall RMS (t>{SETTLE:.0f}s): {overall:.4f} rad "
          f"({np.degrees(overall):.2f} deg)")
    print(f"per-joint RMS: {per_joint[0]:.4f}, {per_joint[1]:.4f} rad")
    print(f"peak |u|: {np.abs(log['u']).max(axis=0)} N*m")

    rms = rolling_rms(log["err"], round(RMS_WINDOW / DT_CTRL))

    fig, axes = plt.subplots(3, 1, figsize=(11, 9), sharex=False)

    view = log["t"] >= T_END - 20
    for j, ax in enumerate(axes[:2]):
        ax.plot(log["t"][view], log["ref"][view, j], "k--", lw=1,
                label="reference")
        ax.plot(log["t"][view], log["theta"][view, j], lw=1.2,
                label="actual")
        ax.set_ylabel(f"$\\theta_{j+1}$ (rad)")
        ax.legend(loc="upper right", fontsize=8)
        ax.grid(alpha=0.3)
    axes[0].set_title("PID baseline — tracking, last 20 s")

    axes[2].plot(log["t"], rms, lw=1.2, color="tab:red")
    axes[2].axhline(overall, ls="--", color="gray", lw=1,
                    label=f"steady-state RMS = {overall:.4f} rad")
    axes[2].set_ylabel("rolling RMS error (rad)")
    axes[2].set_xlabel("t (s)")
    axes[2].set_ylim(bottom=0)
    axes[2].legend(fontsize=8)
    axes[2].grid(alpha=0.3)
    axes[2].set_title(f"Rolling RMS tracking error ({RMS_WINDOW:.0f} s window)")

    fig.tight_layout()
    fig.savefig("baseline_rms.png", dpi=130)
    print("wrote baseline_rms.png")


if __name__ == "__main__":
    main()
