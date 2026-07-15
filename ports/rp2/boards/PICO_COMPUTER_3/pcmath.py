# Maths helpers for the Pico Computer 3 -- the parts of MMBasic's MATH command
# that ulab doesn't already cover. Statistics, linear algebra, FFT and complex
# math are all in ulab (`import ulab.numpy as np`) + `cmath`, so this module only
# adds the missing verbs: quaternions, 3-D vector helpers, a few DSP conveniences
# (window / sinc / zero-crossing / power spectrum), correlation, chi-square, and
# a PID controller.
#
#     import pcmath
#     q = pcmath.Quat.from_euler(0, 0, math.radians(90))
#     north = q.rotate((1, 0, 0))            # -> (0, 1, 0)
#     w = pcmath.window(256, "hann")
#     r = pcmath.correl(xs, ys)
#     pid = pcmath.PID(2.0, 0.5, 0.1, out_min=0, out_max=255)
#
# ulab is compiled into this firmware, so no rebuild is needed to use this.

import math

from ulab import numpy as np


# ============================================================================
# Quaternions (MMBasic Q_CREATE / Q_MULT / Q_ROTATE / Q_EULER / Q_INVERT).
# Stored as plain floats (w, x, y, z); rotations use the aerospace ZYX (roll
# about x, pitch about y, yaw about z) convention.
# ============================================================================
class Quat:
    def __init__(self, w=1.0, x=0.0, y=0.0, z=0.0):
        self.w = float(w)
        self.x = float(x)
        self.y = float(y)
        self.z = float(z)

    @staticmethod
    def identity():
        return Quat(1.0, 0.0, 0.0, 0.0)

    @staticmethod
    def from_axis(axis, angle):
        """Rotation of `angle` radians about `axis` (a 3-vector)."""
        ax, ay, az = axis
        n = math.sqrt(ax * ax + ay * ay + az * az)
        if n == 0.0:
            return Quat.identity()
        s = math.sin(angle / 2) / n
        return Quat(math.cos(angle / 2), ax * s, ay * s, az * s)

    @staticmethod
    def from_euler(roll, pitch, yaw):
        cr, sr = math.cos(roll / 2), math.sin(roll / 2)
        cp, sp = math.cos(pitch / 2), math.sin(pitch / 2)
        cy, sy = math.cos(yaw / 2), math.sin(yaw / 2)
        return Quat(cr * cp * cy + sr * sp * sy,
                    sr * cp * cy - cr * sp * sy,
                    cr * sp * cy + sr * cp * sy,
                    cr * cp * sy - sr * sp * cy)

    def __mul__(self, o):
        return Quat(
            self.w * o.w - self.x * o.x - self.y * o.y - self.z * o.z,
            self.w * o.x + self.x * o.w + self.y * o.z - self.z * o.y,
            self.w * o.y - self.x * o.z + self.y * o.w + self.z * o.x,
            self.w * o.z + self.x * o.y - self.y * o.x + self.z * o.w)

    def conjugate(self):
        return Quat(self.w, -self.x, -self.y, -self.z)

    def norm(self):
        return math.sqrt(self.w ** 2 + self.x ** 2 + self.y ** 2 + self.z ** 2)

    def normalise(self):
        n = self.norm()
        if n == 0.0:
            return Quat.identity()
        return Quat(self.w / n, self.x / n, self.y / n, self.z / n)

    normalize = normalise

    def inverse(self):
        n2 = self.w ** 2 + self.x ** 2 + self.y ** 2 + self.z ** 2
        if n2 == 0.0:
            return Quat.identity()
        return Quat(self.w / n2, -self.x / n2, -self.y / n2, -self.z / n2)

    def rotate(self, v):
        """Rotate the 3-vector `v` by this quaternion. Returns a 3-tuple."""
        q = self.normalise()
        p = Quat(0.0, v[0], v[1], v[2])
        r = q * p * q.conjugate()
        return (r.x, r.y, r.z)

    def to_euler(self):
        """(roll, pitch, yaw) in radians."""
        w, x, y, z = self.w, self.x, self.y, self.z
        roll = math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
        t = 2 * (w * y - z * x)
        t = 1.0 if t > 1.0 else -1.0 if t < -1.0 else t
        pitch = math.asin(t)
        yaw = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
        return (roll, pitch, yaw)

    def to_matrix(self):
        """The equivalent 3x3 rotation matrix (an ndarray)."""
        w, x, y, z = self.w, self.x, self.y, self.z
        return np.array([
            [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
            [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
            [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])

    def __repr__(self):
        return "Quat(%g, %g, %g, %g)" % (self.w, self.x, self.y, self.z)


# ============================================================================
# 3-D vectors (MMBasic V_CROSS / V_NORMALISE / V_ROTATE). Operate on any 3-item
# sequence and return tuples.
# ============================================================================
def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vmag(a):
    return math.sqrt(vdot(a, a))


def vunit(a):
    m = vmag(a)
    return (a[0] / m, a[1] / m, a[2] / m) if m else (0.0, 0.0, 0.0)


def vrotate(v, axis, angle):
    """Rotate `v` by `angle` radians about `axis` (Rodrigues' formula)."""
    k = vunit(axis)
    c, s = math.cos(angle), math.sin(angle)
    kv = vcross(k, v)
    kd = vdot(k, v)
    return tuple(v[i] * c + kv[i] * s + k[i] * kd * (1 - c) for i in range(3))


# ============================================================================
# DSP conveniences (MMBasic WINDOW / SINC / CROSSING + FFT power). Array results
# are ulab ndarrays. FFT-based helpers need a power-of-2 length (ulab's FFT).
# ============================================================================
def window(n, kind="hann"):
    """A window function of length n: hann, hamming, blackman, bartlett, rect."""
    if n <= 1:
        return np.ones(n)
    k = kind.lower()
    i = np.arange(n)
    m = n - 1
    if k in ("hann", "hanning"):
        return 0.5 - 0.5 * np.cos(2 * math.pi * i / m)
    if k == "hamming":
        return 0.54 - 0.46 * np.cos(2 * math.pi * i / m)
    if k == "blackman":
        return 0.42 - 0.5 * np.cos(2 * math.pi * i / m) + 0.08 * np.cos(4 * math.pi * i / m)
    if k in ("bartlett", "triangle"):
        t = (i - m / 2) / (m / 2)
        return 1 - np.sqrt(t * t)            # |t|, without np.abs (ulab lacks it)
    if k in ("rect", "boxcar", "none"):
        return np.ones(n)
    raise ValueError("unknown window '%s'" % kind)


def sinc(x):
    """Normalised sinc, sin(pi x)/(pi x) with sinc(0)=1. Scalar or sequence."""
    try:
        n = len(x)
    except TypeError:
        return 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)
    out = np.zeros(n)
    for i in range(n):
        xi = x[i]
        out[i] = 1.0 if xi == 0 else math.sin(math.pi * xi) / (math.pi * xi)
    return out


def crossings(a, level=0.0):
    """Indices where the signal crosses `level` (a sign change of a-level).
    len(crossings(...)) gives the count."""
    idx = []
    prev = None
    for i in range(len(a)):
        v = a[i] - level
        if prev is not None and ((prev < 0 and v >= 0) or (prev > 0 and v <= 0)):
            idx.append(i)
        prev = v
    return idx


def power_spectrum(a, onesided=True):
    """Power spectrum |FFT|^2. One-sided (0..N/2) by default. `a` length must be
    a power of 2 (ulab FFT). Returns an ndarray."""
    spec = np.fft.fft(np.array(a))
    re = np.real(spec)                       # |z|^2 = re^2 + im^2 (ulab lacks abs)
    im = np.imag(spec)
    p = re * re + im * im
    if onesided:
        return p[:len(a) // 2 + 1]
    return p


# ============================================================================
# Statistics not in ulab (MMBasic CORREL / CHI). Pure Python; take any
# sequences.
# ============================================================================
def correl(a, b):
    """Pearson correlation coefficient of two equal-length sequences (-1..1)."""
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    sab = saa = sbb = 0.0
    for i in range(n):
        da = a[i] - ma
        db = b[i] - mb
        sab += da * db
        saa += da * da
        sbb += db * db
    d = math.sqrt(saa * sbb)
    return sab / d if d else 0.0


def chi_square(observed, expected):
    """Chi-square statistic and its p-value for observed vs expected counts.
    Returns (chi2, p). dof = len-1."""
    chi2 = 0.0
    for o, e in zip(observed, expected):
        if e:
            chi2 += (o - e) ** 2 / e
    dof = len(observed) - 1
    return chi2, _gammq(dof / 2.0, chi2 / 2.0)


# --- regularised upper incomplete gamma Q(a,x), for the chi-square p-value ---
_LANCZOS = (676.5203681218851, -1259.1392167224028, 771.32342877765313,
            -176.61502916214059, 12.507343278686905, -0.13857109526572012,
            9.9843695780195716e-6, 1.5056327351493116e-7)


def _lgamma(x):
    if x < 0.5:
        return math.log(math.pi / math.sin(math.pi * x)) - _lgamma(1 - x)
    x -= 1
    a = 0.99999999999980993
    t = x + 7.5
    for i, c in enumerate(_LANCZOS):
        a += c / (x + i + 1)
    return 0.5 * math.log(2 * math.pi) + (x + 0.5) * math.log(t) - t + math.log(a)


def _gammq(a, x):
    if x <= 0.0 or a <= 0.0:
        return 1.0
    if x < a + 1.0:                              # series for P(a,x)
        ap = a
        s = 1.0 / a
        d = s
        for _ in range(300):
            ap += 1.0
            d *= x / ap
            s += d
            if abs(d) < abs(s) * 1e-14:
                break
        return 1.0 - s * math.exp(-x + a * math.log(x) - _lgamma(a))
    b = x + 1.0 - a                              # continued fraction for Q(a,x)
    c = 1e30
    d = 1.0 / b
    h = d
    for i in range(1, 300):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        if abs(d) < 1e-30:
            d = 1e-30
        c = b + an / c
        if abs(c) < 1e-30:
            c = 1e-30
        d = 1.0 / d
        delt = d * c
        h *= delt
        if abs(delt - 1.0) < 1e-14:
            break
    return math.exp(-x + a * math.log(x) - _lgamma(a)) * h


# ============================================================================
# PID controller (MMBasic MATH PID). Call update() each control step with the
# measured value and the time since the last call.
# ============================================================================
class PID:
    def __init__(self, kp, ki, kd, setpoint=0.0, out_min=None, out_max=None):
        self.kp = kp
        self.ki = ki
        self.kd = kd
        self.setpoint = setpoint
        self.out_min = out_min
        self.out_max = out_max
        self._i = 0.0
        self._prev = None

    def reset(self):
        self._i = 0.0
        self._prev = None

    def _clamp(self, v):
        if self.out_min is not None and v < self.out_min:
            return self.out_min
        if self.out_max is not None and v > self.out_max:
            return self.out_max
        return v

    def update(self, measured, dt, setpoint=None):
        if setpoint is not None:
            self.setpoint = setpoint
        err = self.setpoint - measured
        self._i = self._clamp(self._i + self.ki * err * dt)     # integral + anti-windup
        d = 0.0
        if self._prev is not None and dt > 0:
            d = self.kd * (err - self._prev) / dt
        self._prev = err
        return self._clamp(self.kp * err + self._i + d)
