"""Fully-actuated planar double pendulum.

Angle convention: theta1 measured from the +x axis, theta2 relative to
link 1. Gravity acts in -y. Links are uniform rods (COM at l/2,
I = m*l^2/12) with an optional point mass at the tip of link 2.

All physical parameters are plain attributes, mutable at runtime; the
derived dynamics coefficients are recomputed every evaluation so a
mid-run change takes effect on the next substep.
"""

import numpy as np


class DoublePendulum:
    def __init__(self, m1=1.0, m2=1.0, l1=1.0, l2=1.0,
                 friction1=0.5, friction2=0.5, tip_mass=0.0, g=9.81):
        self.m1, self.m2 = m1, m2
        self.l1, self.l2 = l1, l2
        self.friction1, self.friction2 = friction1, friction2
        self.tip_mass = tip_mass
        self.g = g

        # periodic torque disturbance applied at the plant input, per joint
        self.dist_amp = np.zeros(2)      # N*m
        self.dist_freq = 1.0             # Hz, shared
        self.dist_phase = np.array([0.0, np.pi / 3])

        # state [theta1, theta2, omega1, omega2]
        self.state = np.array([-np.pi / 2, 0.0, 0.0, 0.0])
        self.t = 0.0

    def reset(self, theta1=-np.pi / 2, theta2=0.0):
        self.state = np.array([theta1, theta2, 0.0, 0.0])
        self.t = 0.0

    def disturbance(self, t):
        return self.dist_amp * np.sin(
            2 * np.pi * self.dist_freq * t + self.dist_phase)

    def _accel(self, state, tau, t):
        th1, th2, w1, w2 = state
        m1, m2, mt = self.m1, self.m2, self.tip_mass
        l1, l2, g = self.l1, self.l2, self.g
        lc1, lc2 = l1 / 2, l2 / 2
        I1, I2 = m1 * l1**2 / 12, m2 * l2**2 / 12

        a1 = m1 * lc1**2 + I1 + (m2 + mt) * l1**2
        a2 = m2 * lc2**2 + I2 + mt * l2**2
        a3 = (m2 * lc2 + mt * l2) * l1
        b1 = (m1 * lc1 + (m2 + mt) * l1) * g
        b2 = (m2 * lc2 + mt * l2) * g

        c2, s2 = np.cos(th2), np.sin(th2)
        M11 = a1 + a2 + 2 * a3 * c2
        M12 = a2 + a3 * c2
        M22 = a2

        cor1 = -a3 * s2 * (2 * w1 * w2 + w2**2)
        cor2 = a3 * s2 * w1**2
        g1 = b1 * np.cos(th1) + b2 * np.cos(th1 + th2)
        g2 = b2 * np.cos(th1 + th2)

        tau_eff = tau + self.disturbance(t)
        r1 = tau_eff[0] - cor1 - g1 - self.friction1 * w1
        r2 = tau_eff[1] - cor2 - g2 - self.friction2 * w2

        det = M11 * M22 - M12 * M12
        acc1 = (M22 * r1 - M12 * r2) / det
        acc2 = (M11 * r2 - M12 * r1) / det
        return np.array([w1, w2, acc1, acc2])

    def step(self, tau, dt):
        """Advance one RK4 substep with torque held constant."""
        s, t = self.state, self.t
        k1 = self._accel(s, tau, t)
        k2 = self._accel(s + 0.5 * dt * k1, tau, t + 0.5 * dt)
        k3 = self._accel(s + 0.5 * dt * k2, tau, t + 0.5 * dt)
        k4 = self._accel(s + dt * k3, tau, t + dt)
        self.state = s + (dt / 6) * (k1 + 2 * k2 + 2 * k3 + k4)
        self.t += dt

    @property
    def theta(self):
        return self.state[:2]

    @property
    def omega(self):
        return self.state[2:]

    def tip_xy(self):
        th1, th2 = self.state[:2]
        x1 = self.l1 * np.cos(th1)
        y1 = self.l1 * np.sin(th1)
        return (x1 + self.l2 * np.cos(th1 + th2),
                y1 + self.l2 * np.sin(th1 + th2))
