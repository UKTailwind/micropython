# pcmath: quaternions, 3-D vectors, DSP, correlation/chi-square and PID.
# Pure-numeric (no screen), self-verifying. Also runs under test_all.py.

import math

from ulab import numpy as np

import pcmath as M
import testutil as T


def _close(a, b, t=1e-6):
    return abs(a - b) <= t


def _vclose(a, b, t=1e-6):
    return all(_close(x, y, t) for x, y in zip(a, b))


T.section("quaternions")
q = M.Quat.from_axis((0, 0, 1), math.pi / 2)              # 90 deg about +z
T.check(_vclose(q.rotate((1, 0, 0)), (0, 1, 0)), "rotate x -> y")
T.check(_vclose(q.rotate((0, 1, 0)), (-1, 0, 0)), "rotate y -> -x")
T.check(_vclose((q * q).rotate((1, 0, 0)), (-1, 0, 0)), "compose: 2x90 = 180")
qi = q.inverse()
T.check(_vclose((q * qi).rotate((1, 2, 3)), (1, 2, 3)), "q * q^-1 = identity")
e = (0.1, 0.2, 0.3)
T.check(_vclose(M.Quat.from_euler(*e).to_euler(), e), "euler round-trip")
mtx = q.to_matrix()
T.check(_vclose(tuple(np.dot(mtx, np.array([1.0, 0, 0]))), q.rotate((1, 0, 0))),
        "to_matrix matches rotate")

T.section("3-D vectors")
T.check(M.vcross((1, 0, 0), (0, 1, 0)) == (0, 0, 1), "cross")
T.check(M.vdot((1, 2, 3), (4, 5, 6)) == 32, "dot")
T.check(_close(M.vmag((3, 4, 0)), 5), "magnitude")
T.check(_vclose(M.vunit((0, 3, 0)), (0, 1, 0)), "unit")
T.check(_vclose(M.vrotate((1, 0, 0), (0, 0, 1), math.pi / 2), (0, 1, 0)), "rotate (Rodrigues)")

T.section("DSP")
w = M.window(8, "hann")
T.check(_close(w[0], 0) and _close(w[-1], 0), "hann ends at 0")
T.check(_close(M.window(8, "hamming")[0], 0.08, 1e-6), "hamming ends at 0.08")
w16 = M.window(16, "hann")
T.check(all(_close(w16[i], w16[15 - i], 1e-6) for i in range(8)), "window symmetric")
wb = M.window(9, "bartlett")
T.check(_close(wb[0], 0) and _close(wb[4], 1) and _close(wb[-1], 0), "bartlett triangle")
T.check(M.sinc(0) == 1.0 and _close(M.sinc(1), 0, 1e-9), "sinc(0)=1, sinc(1)=0")
T.check(_close(M.sinc(0.5), 2 / math.pi, 1e-6), "sinc(0.5)=2/pi")
xs = np.sin(np.linspace(0.1, 4 * math.pi + 0.1, 200))     # zeros at pi,2pi,3pi,4pi
T.check(len(M.crossings(xs)) == 4, "4 zero-crossings of 2 sine periods")
N, k = 64, 5
sig = np.cos(2 * math.pi * k * np.arange(N) / N)
T.check(int(np.argmax(M.power_spectrum(sig))) == k, "power spectrum peak at bin k")

T.section("statistics")
T.check(_close(M.correl([1, 2, 3, 4, 5], [2, 4, 6, 8, 10]), 1.0), "correl +1")
T.check(_close(M.correl([1, 2, 3, 4, 5], [5, 4, 3, 2, 1]), -1.0), "correl -1")
chi2, p = M.chi_square([10, 12, 8, 15, 5], [10, 10, 10, 10, 10])
T.check(_close(chi2, 5.8), "chi-square statistic")
# closed form for 4 dof: p = e^(-chi2/2) * (1 + chi2/2)
T.check(_close(p, math.exp(-2.9) * (1 + 2.9), 1e-4), "chi-square p-value")

T.section("PID")
# P-only: out = Kp*(setpoint - measurement)
pid = M.PID(2.0, 0.0, 0.0, T=0.1, out_min=0, out_max=255)
T.check(pid.update(10, 0) == 20.0, "P term: 2*(10-0)")
# Integral clamps to the dedicated integrator limit (anti-windup)
pid2 = M.PID(0.0, 1.0, 0.0, T=1.0, out_max=5, int_max=5)
out = 0
for _ in range(100):
    out = pid2.update(10, 0)
T.check(out == 5.0, "integral clamps (anti-windup)")
# Derivative on measurement, band-limited by tau: matches MATHS.c exactly.
pid3 = M.PID(0.0, 0.0, 1.0, tau=0.02, T=0.01)
d1 = pid3.update(0, 0)          # prev_measurement 0->0, differentiator stays 0
d2 = pid3.update(0, 1.0)        # measurement steps 0->1
# differentiator = -(2*Kd*(1-0) + (2*tau-T)*0) / (2*tau+T) = -2/0.05 = -40
T.check(_close(d2, -40.0, 1e-9), "band-limited derivative (tau) matches MMBasic")
# 1 ms floor, as enforced by MMBasic's MATH PID INIT
try:
    M.PID(1.0, 0.0, 0.0, T=0.0005)
    T.check(False, "T < 1 ms rejected")
except ValueError:
    T.check(True, "T < 1 ms rejected")


if __name__ == "__main__":
    T.report()
