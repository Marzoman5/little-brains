"""Acceptance tests from the brief (Part 5). Run: python test_acceptance.py"""

import numpy as np

from sim import DoublePendulum, PID, Reference
from sim.runner import run_loop, rms, DT_CTRL, N_CTX
from cereb import Cerebellum

GAINS = ([120.0, 60.0], [40.0, 20.0], [18.0, 7.0])


def build(module=True, **kw):
    plant = DoublePendulum()
    ref = Reference()
    plant.reset(*ref.theta(0.0))
    pid = PID(*GAINS)
    cer = Cerebellum(N_CTX, 2, DT_CTRL, seed=1, **kw) if module else None
    return plant, pid, ref, cer


def test_lr0_bit_exact():
    plant, pid, ref, _ = build(module=False)
    a = run_loop(plant, pid, ref, t_end=20.0)
    plant, pid, ref, cer = build(lr_fast=0.0, consolidation=True, growth=True)
    b = run_loop(plant, pid, ref, cereb=cer, t_end=20.0, clock_freq=1.0)
    assert np.array_equal(a["theta"], b["theta"])
    assert not b["u_cereb"].any()
    print("PASS lr=0 bit-exact no-op (consolidation+growth enabled)")


def test_zero_init_zero_correction():
    _, _, _, cer = build()
    c = cer.step(np.ones(N_CTX), np.array([0.5, -0.5]))
    assert np.array_equal(c, np.zeros(2))
    print("PASS zero correction on step one")


def test_growth_not_on_noise():
    plant, pid, ref, cer = build(n_basis=1000, growth=True)
    run_loop(plant, pid, ref, cereb=cer, t_end=300.0, clock_freq=1.0,
             noise_std=0.01)
    grew = [e for e in cer.events if "grew" in e[1]]
    assert len(grew) == 0, grew
    print("PASS growth does not fire on sensor noise")


def test_growth_on_capacity_limit():
    plant, pid, ref, cer = build(n_basis=8, growth=True)
    plant.dist_amp = np.array([6.0, 6.0])
    plant.dist_freq = 0.7
    run_loop(plant, pid, ref, cereb=cer, t_end=300.0, clock_freq=0.7)
    grew = [e for e in cer.events if "grew" in e[1]]
    assert len(grew) >= 1, "growth never fired on capacity-limited task"
    print(f"PASS growth fires on capacity limit ({len(grew)} events, "
          f"8 -> {cer.n_basis} units)")


def test_weights_bounded_long_run():
    plant, pid, ref, cer = build(consolidation=True, leak=5e-3)
    plant.dist_amp = np.array([6.0, 6.0])
    plant.dist_freq = 0.7
    log = run_loop(plant, pid, ref, cereb=cer, t_end=600.0, clock_freq=0.7)
    f, s = log["fast_norm"], log["slow_norm"]
    assert np.isfinite(f[-1]) and np.isfinite(s[-1])
    # norms in the last quarter must not exceed 1.5x the second quarter's max
    q2 = max(f[15000:30000].max(), s[15000:30000].max())
    q4 = max(f[45000:].max(), s[45000:].max())
    assert q4 < 1.5 * q2, (q2, q4)
    print(f"PASS weight norms bounded over 600 s disturbed run "
          f"(fast {f[-1]:.1f}, slow {s[-1]:.1f})")


def test_output_never_exceeds_clamp():
    plant, pid, ref, cer = build(clamp=5.0)
    plant.dist_amp = np.array([8.0, 8.0])
    log = run_loop(plant, pid, ref, cereb=cer, t_end=60.0, clock_freq=0.7)
    assert np.abs(log["u_cereb"]).max() <= 5.0 + 1e-12
    print(f"PASS correction never exceeds clamp "
          f"(peak {np.abs(log['u_cereb']).max():.3f} of 5.0)")


if __name__ == "__main__":
    test_lr0_bit_exact()
    test_zero_init_zero_correction()
    test_output_never_exceeds_clamp()
    test_growth_on_capacity_limit()
    test_growth_not_on_noise()
    test_weights_bounded_long_run()
    print("\nALL ACCEPTANCE TESTS PASS")
