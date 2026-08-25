"""Independent PID per joint. Gains are arrays, mutable at runtime."""

import numpy as np


class PID:
    def __init__(self, kp, ki, kd, u_max=50.0):
        self.kp = np.asarray(kp, dtype=float)
        self.ki = np.asarray(ki, dtype=float)
        self.kd = np.asarray(kd, dtype=float)
        self.u_max = u_max                  # per-joint torque authority
        self.integ = np.zeros_like(self.kp)

    def reset(self):
        self.integ[:] = 0.0

    def step(self, err, err_dot, dt):
        self.integ += err * dt
        u = self.kp * err + self.ki * self.integ + self.kd * err_dot
        # anti-windup: freeze the integrator on the saturated channel
        sat = np.abs(u) > self.u_max
        self.integ[sat] -= err[sat] * dt
        return np.clip(u, -self.u_max, self.u_max)
