"""Smooth periodic joint-space reference: per joint, a sum of two
sinusoids at incommensurate frequencies (ratio involving sqrt(2) and the
golden ratio) so the trajectory never repeats exactly."""

import numpy as np

_PHI = (1 + np.sqrt(5)) / 2


class Reference:
    def __init__(self):
        # joint 1 swings about hanging-down (-pi/2)
        self.center = np.array([-np.pi / 2, 0.6])
        self.amp = np.array([[0.55, 0.25],
                             [0.45, 0.22]])
        self.freq = np.array([[0.30, 0.30 * np.sqrt(2)],
                              [0.45, 0.45 * _PHI]])   # Hz

    def theta(self, t):
        w = 2 * np.pi * self.freq
        return self.center + (self.amp * np.sin(w * t)).sum(axis=1)

    def theta_dot(self, t):
        w = 2 * np.pi * self.freq
        return (self.amp * w * np.cos(w * t)).sum(axis=1)
