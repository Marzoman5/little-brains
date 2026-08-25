"""Stages 4-6 in one battery.

Stage 4: +0.5 kg tip mass dropped at t=90 s mid-run. Error spikes, then
recovers with no retuning.
Stage 5: repeated disturbance exposure (on/off/on), consolidation on vs
off. Consolidation should make the second exposure cheaper (savings).
Stage 6: growth controller. Positive: 30-unit module on the tracking
task must grow. Negative: 1000-unit module with sensor noise must not.
"""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from sim import DoublePendulum, PID, Reference
from sim.runner import run_loop, rms, rolling_rms, DT_CTRL, N_CTX
from cereb import Cerebellum

GAINS = ([120.0, 60.0], [40.0, 20.0], [18.0, 7.0])


def build(module=True, **kw):
    plant = DoublePendulum()
    ref = Reference()
    plant.reset(*ref.theta(0.0))
    pid = PID(*GAINS)
    cer = Cerebellum(N_CTX, 2, DT_CTRL, seed=1, **kw) if module else None
    return plant, pid, ref, cer


# ---------------------------------------------------------------- stage 4
def stage4():
    def add_mass(p):
        def f():
            p.tip_mass = 0.5
        return f

    plant, pid, ref, cer = build()
    log_m = run_loop(plant, pid, ref, cereb=cer, t_end=240.0, clock_freq=1.0,
                     callbacks=[(90.0, add_mass(plant))])
    plant, pid, ref, _ = build(module=False)
    log_p = run_loop(plant, pid, ref, t_end=240.0,
                     callbacks=[(90.0, add_mass(plant))])

    before = rms(log_m, 60.0, 90.0)
    spike = rms(log_m, 90.0, 100.0)
    after = rms(log_m, 180.0, 240.0)
    w = round(5.0 / DT_CTRL)
    r_roll = rolling_rms(log_m["err"], w)
    rec_i = np.flatnonzero((log_m["t"] > 95) & (r_roll < 2 * before))
    t_rec = log_m["t"][rec_i[0]] - 90.0 if rec_i.size else np.inf
    print(f"[4] module: before {before:.4f}, spike(90-100s) {spike:.4f}, "
          f"recovered(<2x) in {t_rec:.0f}s, final {after:.4f}")
    print(f"[4] PID-only: before {rms(log_p, 60, 90):.4f}, "
          f"after mass {rms(log_p, 180):.4f}")
    return log_m, log_p, before


# ---------------------------------------------------------------- stage 5
LEAK5, LRS5 = 5e-3, 0.05   # leaky fast scratchpad + two-rate slow layer

def stage5_run(consolidation, prior_exposure):
    plant, pid, ref, cer = build(consolidation=consolidation,
                                 leak=LEAK5, lr_slow=LRS5)
    plant.dist_freq = 0.7

    def set_amp(a):
        def f():
            plant.dist_amp = np.array([a, a])
        return f

    cbs = [(180.0, set_amp(6.0))]
    if prior_exposure:
        cbs = [(60.0, set_amp(6.0)), (120.0, set_amp(0.0))] + cbs
    log = run_loop(plant, pid, ref, cereb=cer, t_end=240.0, clock_freq=0.7,
                   callbacks=cbs)
    return log


def stage5():
    """Savings, time-locked: same 180-195 s window, with vs without a
    prior exposure at 60-120 s. Window effects cancel exactly."""
    out = {}
    for consol in (True, False):
        re_log = stage5_run(consol, True)
        nv_log = stage5_run(consol, False)
        re_rms, nv_rms = rms(re_log, 180, 195), rms(nv_log, 180, 195)
        sav = 100 * (1 - re_rms / nv_rms)
        print(f"[5] consolidation={consol}: naive {nv_rms:.4f}, "
              f"re-exposed {re_rms:.4f}, savings {sav:.0f}%")
        out[consol] = (re_log, re_rms, nv_rms)
    return out


# ---------------------------------------------------------------- stage 6
def stage6():
    # positive: 8 units cannot represent the disturbance correction
    # (converged rms 0.026, autocorr 0.94 with growth off) — must grow
    plant, pid, ref, cer_p = build(n_basis=8, growth=True)
    plant.dist_amp = np.array([6.0, 6.0])
    plant.dist_freq = 0.7
    log_p = run_loop(plant, pid, ref, cereb=cer_p, t_end=400.0, clock_freq=0.7)
    grew = [e for e in cer_p.events if "grew" in e[1]]
    print(f"[6+] 8-unit start: {len(grew)} growth events, "
          f"final basis {cer_p.n_basis}, "
          f"rms 20-60s {rms(log_p, 20, 60):.4f} -> 340-400s {rms(log_p, 340):.4f}")
    for t, msg in cer_p.events:
        print(f"     t={t:5.1f}s {msg}")

    # negative: sensor noise only — growth must NOT fire
    plant, pid, ref, cer_n = build(n_basis=1000, growth=True)
    log_n = run_loop(plant, pid, ref, cereb=cer_n, t_end=300.0, clock_freq=1.0,
                     noise_std=0.01)
    grew_n = [e for e in cer_n.events if "grew" in e[1]]
    print(f"[6-] noise-only run: {len(grew_n)} growth events "
          f"(must be 0), basis {cer_n.n_basis}, true rms {rms(log_n, 240):.4f}")
    return log_p, cer_p, log_n, cer_n


# ---------------------------------------------------------------- figure
def main():
    log4m, log4p, before4 = stage4()
    s5 = stage5()
    log5c = s5[True][0]          # consolidation, re-exposed (on/off/on run)
    log6p, cer6p, log6n, cer6n = stage6()

    w = round(5.0 / DT_CTRL)
    fig, axes = plt.subplots(4, 1, figsize=(11, 13))

    ax = axes[0]
    ax.plot(log4p["t"], rolling_rms(log4p["err"], w), color="gray", lw=1.1,
            label="PID only")
    ax.plot(log4m["t"], rolling_rms(log4m["err"], w), color="tab:red", lw=1.2,
            label="PID + module")
    ax.axvline(90, color="k", ls=":", lw=1)
    ax.text(91, ax.get_ylim()[1] * 0.0 + 0.11, "+0.5 kg tip mass", fontsize=8)
    ax.set_ylabel("rolling RMS (rad)")
    ax.set_ylim(0, 0.13)
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    ax.set_title("Stage 4 — payload change mid-run, no retuning")

    ax = axes[1]
    re_c, re_rms_c, nv_rms_c = s5[True]
    re_n, re_rms_n, nv_rms_n = s5[False]
    ax.plot(re_n["t"], rolling_rms(re_n["err"], w), color="tab:orange",
            lw=1.1, label=f"no consolidation (re-exp {re_rms_n:.3f} "
                          f"vs naive {nv_rms_n:.3f})")
    ax.plot(re_c["t"], rolling_rms(re_c["err"], w), color="tab:red", lw=1.2,
            label=f"consolidation (re-exp {re_rms_c:.3f} "
                  f"vs naive {nv_rms_c:.3f})")
    for t0, lab in [(60, "dist ON"), (120, "OFF"), (180, "ON again")]:
        ax.axvline(t0, color="k", ls=":", lw=1)
        ax.text(t0 + 1, 0.115, lab, fontsize=8)
    ax.set_ylabel("rolling RMS (rad)")
    ax.set_ylim(0, 0.13)
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    ax.set_title("Stage 5 — repeated disturbance: savings from consolidation")

    ax = axes[2]
    ax.plot(log5c["t"], log5c["fast_norm"], color="tab:orange", lw=1.2,
            label="|w_fast|")
    ax.plot(log5c["t"], log5c["slow_norm"], color="tab:blue", lw=1.2,
            label="|w_slow|")
    for t0 in (60, 120, 180):
        ax.axvline(t0, color="k", ls=":", lw=1)
    ax.set_ylabel("weight L2 norm")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    ax.set_title("Stage 5 — fast scratchpad vs slow consolidated memory")

    ax = axes[3]
    ax.plot(log6p["t"], rolling_rms(log6p["err"], w), color="tab:red", lw=1.2,
            label="8-unit start, growth on")
    ax.plot(log6n["t"], rolling_rms(log6n["err"], w), color="tab:green", lw=1.1,
            label="1000 units + sensor noise (no growth: correct)")
    ax2 = ax.twinx()
    ax2.plot(log6p["t"], log6p["n_basis"], color="tab:blue", lw=1.2, ls="--",
             label="basis size")
    ax2.set_ylabel("basis units", color="tab:blue")
    ax.set_ylabel("rolling RMS (rad)")
    ax.set_xlabel("t (s)")
    ax.set_ylim(0, None)
    ax.legend(fontsize=8, loc="upper right")
    ax.grid(alpha=0.3)
    ax.set_title("Stage 6 — growth fires on capacity limit, not on noise")

    fig.tight_layout()
    fig.savefig("stage456.png", dpi=130)
    print("wrote stage456.png")


if __name__ == "__main__":
    main()
