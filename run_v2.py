"""v1 (decorrelation on tracking error) vs v2 (feedback-error learning +
NLMS + competence gate), pendulum tracking task with disturbance.

Metrics that matter for "drop a brain in and never regret it":
  worst  — max rolling RMS relative to the PID's own steady level during
           the first 120 s (worse-than-baseline transient)
  t50    — time to hold 50% of the PID's error, sustained
  floor  — RMS over the last 30 s
"""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from sim import DoublePendulum, PID, Reference
from sim.runner import run_loop, rms, rolling_rms, DT_CTRL, N_CTX
from cereb import Cerebellum

GAINS = ([120.0, 60.0], [40.0, 20.0], [18.0, 7.0])
T_END = 240.0


def build(module=True, **kw):
    plant = DoublePendulum()
    plant.dist_amp = np.array([6.0, 6.0])
    plant.dist_freq = 0.7
    ref = Reference()
    plant.reset(*ref.theta(0.0))
    pid = PID(*GAINS)
    cer = Cerebellum(N_CTX, 2, DT_CTRL, seed=1, **kw) if module else None
    return plant, pid, ref, cer


def metrics(log, base):
    w = round(5.0 / DT_CTRL)
    roll = rolling_rms(log["err"], w)
    m = log["t"] >= 10.0                      # skip the shared PID transient
    worst = roll[m].max() / base
    below = (roll <= 0.5 * base) & (log["t"] >= 10.0)
    t50 = np.inf
    idx = np.flatnonzero(below)
    for i in idx:                              # first crossing held for 10 s
        j = i + round(10.0 / DT_CTRL)
        if j < len(below) and below[i:j].all():
            t50 = log["t"][i]
            break
    floor = rms(log, T_END - 30.0)
    return worst, t50, floor, roll


def main():
    plant, pid, ref, _ = build(module=False)
    log_pid = run_loop(plant, pid, ref, t_end=T_END)
    base = rms(log_pid, 10.0)
    print(f"PID-only steady RMS: {base:.4f} rad")

    runs = {}
    # v1: decorrelation on tracking error, tuned constants from stages 2-3
    plant, pid, ref, cer = build(lr_fast=0.5)
    runs["v1 decorrelation"] = run_loop(plant, pid, ref, cereb=cer,
                                        t_end=T_END, clock_freq=0.7)
    # v2: FEL teacher + NLMS + competence gate (mu dimensionless)
    plant, pid, ref, cer2 = build(lr_fast=0.5, nlms=True, gate=True)
    runs["v2 FEL+NLMS+gate"] = run_loop(plant, pid, ref, cereb=cer2,
                                        t_end=T_END, clock_freq=0.7,
                                        teacher="u_fb")

    fig, ax = plt.subplots(figsize=(11, 5))
    w = round(5.0 / DT_CTRL)
    ax.plot(log_pid["t"], rolling_rms(log_pid["err"], w), color="gray",
            lw=1.1, label=f"PID only ({base:.4f})")
    for (name, log), c in zip(runs.items(), ["tab:orange", "tab:red"]):
        worst, t50, floor, roll = metrics(log, base)
        print(f"{name}: worst {worst:.2f}x baseline, t50 {t50:.0f}s, "
              f"floor {floor:.4f} ({100 * floor / base:.0f}%)")
        ax.plot(log["t"], roll, color=c, lw=1.3,
                label=f"{name} (worst {worst:.2f}x, t50 {t50:.0f}s, "
                      f"floor {floor:.4f})")
    ax.axhline(base, color="gray", ls="--", lw=0.8)
    ax.set_xlabel("t (s)")
    ax.set_ylabel("rolling RMS error (rad)")
    ax.set_ylim(0, None)
    ax.legend(fontsize=9)
    ax.grid(alpha=0.3)
    ax.set_title("v1 vs v2 — disturbance task, from cold start")
    fig.tight_layout()
    fig.savefig("v2_benchmark.png", dpi=130)
    print("wrote v2_benchmark.png")


if __name__ == "__main__":
    main()
