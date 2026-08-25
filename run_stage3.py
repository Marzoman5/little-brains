"""Stage 3: periodic torque disturbance (6 N*m @ 0.7 Hz at both joints).

Four questions:
  1. How much does the disturbance hurt the PID?
  2. Does the module cancel it (down to its own no-disturbance floor)?
  3. Can ANY PID retuning do the same? (64-point gain grid says no)
  4. Are the phase channels the mechanism? (mismatched clock ablation)
"""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from sim import DoublePendulum, PID, Reference
from sim.runner import run_loop, rms, rolling_rms, DT_CTRL, N_CTX
from cereb import Cerebellum

GAINS = ([120.0, 60.0], [40.0, 20.0], [18.0, 7.0])
DIST_AMP, DIST_FREQ = 6.0, 0.7


def build(dist=True, module=False, gains=GAINS, **cereb_kw):
    plant = DoublePendulum()
    if dist:
        plant.dist_amp = np.array([DIST_AMP, DIST_AMP])
        plant.dist_freq = DIST_FREQ
    ref = Reference()
    plant.reset(*ref.theta(0.0))
    pid = PID(*gains)
    cer = Cerebellum(N_CTX, 2, DT_CTRL, seed=1, **cereb_kw) if module else None
    return plant, pid, ref, cer


def retune_grid():
    """Best steady-state RMS over a grid of gain scalings, disturbance on."""
    best = (np.inf, None)
    results = []
    scales = [0.5, 1.0, 2.0, 4.0]
    for sp in scales:
        for si in scales:
            for sd in scales:
                g = ([sp * GAINS[0][0], sp * GAINS[0][1]],
                     [si * GAINS[1][0], si * GAINS[1][1]],
                     [sd * GAINS[2][0], sd * GAINS[2][1]])
                plant, pid, ref, _ = build(gains=g)
                log = run_loop(plant, pid, ref, t_end=30.0)
                if not np.all(np.isfinite(log["theta"])) or \
                        np.abs(log["err"]).max() > 3.0:
                    results.append((sp, si, sd, np.nan))
                    continue
                r = rms(log, 10.0)
                results.append((sp, si, sd, r))
                if r < best[0]:
                    best = (r, (sp, si, sd))
    return best, results


def main():
    # 1. reference floor and disturbance damage
    plant, pid, ref, _ = build(dist=False)
    log_floor = run_loop(plant, pid, ref, t_end=60.0)
    floor = rms(log_floor, 5.0)

    plant, pid, ref, _ = build()
    log_pid = run_loop(plant, pid, ref, t_end=120.0)
    pid_dist = rms(log_pid, 10.0)

    # 2. module, correct clock
    plant, pid, ref, cer = build(module=True)
    log_mod = run_loop(plant, pid, ref, cereb=cer, t_end=180.0,
                       clock_freq=DIST_FREQ)
    mod_dist = rms(log_mod, 150.0)

    # 4. ablation: no phase channels (clock_freq=0 makes sin/cos constant;
    #    normalisation flattens a constant channel to ~0)
    plant, pid, ref, cer_w = build(module=True)
    log_wrong = run_loop(plant, pid, ref, cereb=cer_w, t_end=180.0,
                         clock_freq=0.0)
    mod_wrong = rms(log_wrong, 150.0)

    # 3. retuning grid
    (best_rms, best_scales), grid = retune_grid()
    n_unstable = sum(1 for *_, r in grid if not np.isfinite(r))

    print(f"PID no-disturbance floor:      {floor:.4f} rad")
    print(f"PID with disturbance:          {pid_dist:.4f} rad")
    print(f"best retuned PID (of 64):      {best_rms:.4f} rad  "
          f"scales kp,ki,kd={best_scales}  ({n_unstable} unstable)")
    print(f"PID+module (with clock):       {mod_dist:.4f} rad")
    print(f"PID+module (no phase chans):   {mod_wrong:.4f} rad")
    print(f"nan events: {cer.nan_events}, peak |u_c| {np.abs(log_mod['u_cereb']).max():.2f}")

    # ---- plots ----
    fig, axes = plt.subplots(3, 1, figsize=(11, 10))
    w = round(5.0 / DT_CTRL)

    ax = axes[0]
    ax.plot(log_pid["t"], rolling_rms(log_pid["err"], w), color="gray",
            lw=1.2, label=f"PID, disturbed ({pid_dist:.4f})")
    ax.axhline(best_rms, color="black", ls=":", lw=1.2,
               label=f"best of 64 retuned PIDs ({best_rms:.4f})")
    ax.plot(log_wrong["t"], rolling_rms(log_wrong["err"], w),
            color="tab:orange", lw=1.2,
            label=f"module, no phase channels ({mod_wrong:.4f})")
    ax.plot(log_mod["t"], rolling_rms(log_mod["err"], w), color="tab:red",
            lw=1.2, label=f"module, with clock ({mod_dist:.4f})")
    ax.axhline(floor, color="tab:blue", ls="--", lw=1,
               label=f"PID floor, no disturbance ({floor:.4f})")
    ax.set_ylabel("rolling RMS error (rad)")
    ax.set_ylim(0, None)
    ax.legend(fontsize=8, loc="upper right")
    ax.grid(alpha=0.3)
    ax.set_title(f"Stage 3 — {DIST_AMP:.0f} N·m @ {DIST_FREQ} Hz disturbance")

    # error spectrum, steady state
    ax = axes[1]
    for log, c, lab in [(log_pid, "gray", "PID"),
                        (log_mod, "tab:red", "PID + module")]:
        e = log["err"][log["t"] >= log["t"][-1] - 60][:, 0]
        f = np.fft.rfftfreq(len(e), DT_CTRL)
        amp = np.abs(np.fft.rfft(e)) / len(e)
        ax.semilogy(f, amp, color=c, lw=1, label=lab)
    ax.axvline(DIST_FREQ, color="k", ls=":", lw=1, label="disturbance freq")
    ax.set_xlim(0, 4)
    ax.set_xlabel("Hz")
    ax.set_ylabel("|E1(f)| (rad)")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    ax.set_title("Joint-1 error spectrum (last 60 s)")

    # correction vs disturbance, antiphase overlay
    ax = axes[2]
    view = log_mod["t"] >= 174.0
    tt = log_mod["t"][view]
    p = DoublePendulum()
    p.dist_amp = np.array([DIST_AMP, DIST_AMP]); p.dist_freq = DIST_FREQ
    dist = np.array([p.disturbance(t) for t in tt])
    ax.plot(tt, dist[:, 0], color="k", lw=1.2, label="disturbance, joint 1")
    ax.plot(tt, log_mod["u_cereb"][view, 0], color="tab:red", lw=1.2,
            label="module correction, joint 1")
    ax.plot(tt, dist[:, 0] + log_mod["u_cereb"][view, 0], color="tab:green",
            lw=1, ls="--", label="sum (residual)")
    ax.set_xlabel("t (s)")
    ax.set_ylabel("torque (N·m)")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    ax.set_title("Learned correction is the disturbance, inverted "
                 "(plus the stage-2 gravity terms)")

    fig.tight_layout()
    fig.savefig("stage3_disturbance.png", dpi=130)
    print("wrote stage3_disturbance.png")


if __name__ == "__main__":
    main()
