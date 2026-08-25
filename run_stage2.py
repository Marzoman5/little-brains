"""Stage 2: fixed basis, fast layer only, trajectory tracking (no
disturbance). Compares PID alone vs PID + module on the identical task,
plus the lr=0 bit-exactness and zero-init acceptance checks."""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from sim import DoublePendulum, PID, Reference
from sim.runner import run_loop, rms, rolling_rms, DT_CTRL, N_CTX
from cereb import Cerebellum

GAINS = ([120.0, 60.0], [40.0, 20.0], [18.0, 7.0])
T_END = 120.0


def fresh(lr_fast=0.5, **kw):
    plant = DoublePendulum()
    ref = Reference()
    plant.reset(*ref.theta(0.0))
    pid = PID(*GAINS)
    cer = Cerebellum(N_CTX, 2, DT_CTRL, lr_fast=lr_fast, seed=1, **kw)
    return plant, pid, ref, cer


def acceptance_checks():
    # lr=0 must be bit-exact identical to PID alone
    p1, pid1, ref1, _ = fresh()
    log_a = run_loop(p1, pid1, ref1, cereb=None, t_end=20.0)
    p2, pid2, ref2, cer = fresh(lr_fast=0.0)
    log_b = run_loop(p2, pid2, ref2, cereb=cer, t_end=20.0, clock_freq=1.0)
    assert np.array_equal(log_a["theta"], log_b["theta"]), "lr=0 not bit-exact"
    assert np.array_equal(log_b["u_cereb"], np.zeros_like(log_b["u_cereb"]))
    print("PASS lr=0 bit-exact no-op")

    # zero-initialised module: exactly zero correction on step one
    _, _, _, cer = fresh()
    c = cer.step(np.ones(N_CTX), np.array([0.5, -0.5]))
    assert np.array_equal(c, np.zeros(2)), "first correction not zero"
    print("PASS zero correction on step one")


def main():
    acceptance_checks()

    plant, pid, ref, _ = fresh()
    log_pid = run_loop(plant, pid, ref, t_end=T_END)

    plant, pid, ref, cer = fresh()
    log_mod = run_loop(plant, pid, ref, cereb=cer, t_end=T_END, clock_freq=1.0)

    base = rms(log_pid, 5.0)
    final = rms(log_mod, T_END - 30.0)
    print(f"PID-only steady RMS:        {base:.4f} rad")
    print(f"PID+module RMS (last 30s):  {final:.4f} rad  "
          f"({100 * final / base:.1f}% of baseline)")
    print(f"mean sparsity (last 30s):   "
          f"{log_mod['sparsity'][log_mod['t'] > T_END - 30].mean() * 100:.1f}% active")
    print(f"peak |u_cereb|: {np.abs(log_mod['u_cereb']).max():.2f} N*m "
          f"(clamp {cer.clamp})")
    assert np.abs(log_mod["u_cereb"]).max() <= cer.clamp + 1e-12
    print(f"nan events: {cer.nan_events}")

    w = round(5.0 / DT_CTRL)
    fig, axes = plt.subplots(3, 1, figsize=(11, 9))

    axes[0].plot(log_pid["t"], rolling_rms(log_pid["err"], w), lw=1.2,
                 label="PID alone", color="gray")
    axes[0].plot(log_mod["t"], rolling_rms(log_mod["err"], w), lw=1.2,
                 label="PID + module (fast only)", color="tab:red")
    axes[0].axhline(base, ls="--", color="gray", lw=0.8)
    axes[0].set_ylabel("rolling RMS error (rad)")
    axes[0].set_ylim(bottom=0)
    axes[0].legend(fontsize=9)
    axes[0].grid(alpha=0.3)
    axes[0].set_title("Stage 2 — trajectory tracking, fast layer only")

    axes[1].plot(log_mod["t"], log_mod["fast_norm"], lw=1.2, color="tab:blue")
    axes[1].set_ylabel("|w_fast| L2")
    axes[1].grid(alpha=0.3)

    axes[2].plot(log_mod["t"], log_mod["sparsity"] * 100, lw=0.8,
                 color="tab:green")
    axes[2].set_ylabel("% units active")
    axes[2].set_xlabel("t (s)")
    axes[2].grid(alpha=0.3)

    fig.tight_layout()
    fig.savefig("stage2_fast_layer.png", dpi=130)
    print("wrote stage2_fast_layer.png")


if __name__ == "__main__":
    main()
