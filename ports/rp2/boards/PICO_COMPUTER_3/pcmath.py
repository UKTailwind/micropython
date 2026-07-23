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
#     pid = pcmath.PID(2.0, 0.5, 0.1, tau=0.02, T=0.01, out_min=0, out_max=255)
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
# PID controller -- numerically identical to MMBasic's MATH PID (PicoMite
# MATHS.c PIDController_Update, the "Phil's Lab" band-limited form):
#   * trapezoidal (Tustin) integral with a dedicated anti-windup clamp,
#   * derivative taken on the MEASUREMENT (no setpoint kick), band-limited by
#     a first-order low-pass of time constant `tau`. A unit step in the
#     measurement gives a derivative kick of 2*Kd/(2*tau + T), so larger `tau`
#     tames both the kick and measurement noise. Set `tau` > 0 whenever Kd > 0:
#     tau == 0 is degenerate (the recursion pole sits at +1 and the term
#     integrates rather than differentiates); it is only safe when Kd == 0.
#     As with MMBasic, a few times the sample time (e.g. tau = 2..5 * T) is a
#     sane starting point,
#   * a FIXED sample time `T` seconds -- the maths assume exactly T between
#     calls, so run update() on a fixed schedule. MMBasic's floor is
#     T >= 0.001 (1 ms) and this class enforces the same.
#
#     pid = pcmath.PID(2.0, 0.5, 0.1, tau=0.02, T=0.01,
#                      out_min=0, out_max=255, int_min=-255, int_max=255)
#     drive = pid.update(setpoint, measured)     # call every T seconds
#
# The constructor arguments are MMBasic's 9 PIDController config fields in
# order: Kp, Ki, Kd, tau, (limMin, limMax), (limMinInt, limMaxInt), T. Unlike
# MMBasic -- where a zeroed limMinInt/limMaxInt clamps the integrator to 0 and
# kills integral action -- omitting int_min/int_max here defaults them to the
# output limits (out_min/out_max), so integral action works out of the box.
# Pass int_min/int_max explicitly for a tighter (or looser) anti-windup clamp.
# Pass a callback to .start() to run it in the background off a machine.Timer --
# the port's equivalent of MATH PID START / STOP.
# ============================================================================
class PID:
    def __init__(self, kp, ki, kd, tau=0.0, T=0.01,
                 out_min=None, out_max=None, int_min=None, int_max=None):
        if T < 0.001:
            raise ValueError("T must be >= 0.001 s (1 ms)")
        self.kp = kp
        self.ki = ki
        self.kd = kd
        self.tau = tau                 # derivative low-pass time constant
        self.T = T                     # fixed sample time (seconds)
        self.out_min = out_min
        self.out_max = out_max
        # MMBasic keeps a separate integrator clamp (limMinInt/limMaxInt);
        # default it to the output clamp when not given.
        self.int_min = int_min if int_min is not None else out_min
        self.int_max = int_max if int_max is not None else out_max
        self.integrator = 0.0
        self.prev_error = 0.0
        self.differentiator = 0.0
        self.prev_measurement = 0.0
        self.out = 0.0
        self._timer = None

    def reset(self):
        self.integrator = 0.0
        self.prev_error = 0.0
        self.differentiator = 0.0
        self.prev_measurement = 0.0
        self.out = 0.0

    def update(self, setpoint, measurement):
        error = setpoint - measurement
        proportional = self.kp * error
        # integral (trapezoidal) with anti-windup clamp
        self.integrator += 0.5 * self.ki * self.T * (error + self.prev_error)
        if self.int_max is not None and self.integrator > self.int_max:
            self.integrator = self.int_max
        elif self.int_min is not None and self.integrator < self.int_min:
            self.integrator = self.int_min
        # derivative on measurement, band-limited (bilinear-transform LPF, tau).
        # Minus sign: derivative on measurement, not error. 2*tau + T > 0 since
        # T >= 1 ms, so no divide-by-zero even when tau == 0 (raw differentiator).
        self.differentiator = -(
            2.0 * self.kd * (measurement - self.prev_measurement)
            + (2.0 * self.tau - self.T) * self.differentiator
        ) / (2.0 * self.tau + self.T)
        out = proportional + self.integrator + self.differentiator
        if self.out_max is not None and out > self.out_max:
            out = self.out_max
        elif self.out_min is not None and out < self.out_min:
            out = self.out_min
        self.out = out
        self.prev_error = error
        self.prev_measurement = measurement
        return out

    def start(self, callback):
        """Run `callback(self)` every T seconds off a machine.Timer -- the
        port's MATH PID START. MMBasic fires a BASIC interrupt sub each tick;
        here your callback reads the sensor, calls self.update(setpoint,
        measurement) and drives the output. Runs in soft-IRQ context, so keep
        it short. Call .stop() to end it."""
        import machine

        self.stop()
        period = int(self.T * 1000 + 0.5)      # ms; T >= 0.001 -> >= 1 ms
        self._timer = machine.Timer(-1)
        self._timer.init(period=period, mode=machine.Timer.PERIODIC,
                         callback=lambda t: callback(self))

    def stop(self):
        """Stop a background controller started with .start() (MATH PID STOP)."""
        if self._timer is not None:
            self._timer.deinit()
            self._timer = None


# --- Sensor fusion (MMBasic MATH SENSORFUSION) -------------------------------
class AHRS:
    """Attitude estimation from IMU readings (MMBasic MATH SENSORFUSION):
    fuse accelerometer + gyroscope (+ optional magnetometer) samples into
    roll/pitch/yaw with the Madgwick or Mahony filters -- both ported
    verbatim from MMBasic (PicoMite MATHS.c), including its defaults
    (beta=0.5; Kp=10, Ki=0) and its diverged-filter recovery.

        imu = pcmath.AHRS()
        while True:
            ax, ay, az, gx, gy, gz = read_imu()      # g's and rad/s
            roll, pitch, yaw = imu.madgwick(ax, ay, az, gx, gy, gz)

    Gyro rates are radians/second; angles return in radians (use
    math.degrees()). dt=None times itself between calls (MMBasic's
    AHRSTimer, capped at 1 s); pass dt explicitly for recorded data.
    Magnetometer axes are optional -- omitted = 6-axis IMU mode."""

    def __init__(self):
        self.q = [1.0, 0.0, 0.0, 0.0]
        self._eint = [0.0, 0.0, 0.0]
        self._last = None

    def reset(self):
        """Back to the identity orientation (and clear the Mahony integral)."""
        self.q = [1.0, 0.0, 0.0, 0.0]
        self._eint = [0.0, 0.0, 0.0]
        self._last = None

    def _dt(self, dt):
        import time

        if dt is not None:
            return dt
        now = time.ticks_ms()
        if self._last is None:
            d = 0.0
        else:
            d = time.ticks_diff(now, self._last) / 1000.0
        self._last = now
        return d if d < 1.0 else 1.0

    def _store(self, q1, q2, q3, q4):
        # MMBasic StoreQuaternion: normalise; reset to identity if diverged.
        norm = math.sqrt(q1 * q1 + q2 * q2 + q3 * q3 + q4 * q4)
        if norm == 0.0 or not (norm == norm) or norm == float("inf"):
            q1, q2, q3, q4 = 1.0, 0.0, 0.0, 0.0
        else:
            norm = 1.0 / norm
            q1 *= norm
            q2 *= norm
            q3 *= norm
            q4 *= norm
        self.q[0], self.q[1], self.q[2], self.q[3] = q1, q2, q3, q4
        return q1, q2, q3, q4

    def _angles(self, q1, q2, q3, q4):
        ysqr = q3 * q3
        t0 = 2.0 * (q1 * q2 + q3 * q4)
        t1 = 1.0 - 2.0 * (q2 * q2 + ysqr)
        roll = math.atan2(t0, t1)
        t2 = 2.0 * (q1 * q3 - q4 * q2)
        t2 = 1.0 if t2 > 1.0 else (-1.0 if t2 < -1.0 else t2)
        pitch = math.asin(t2)
        t3 = 2.0 * (q1 * q4 + q2 * q3)
        t4 = 1.0 - 2.0 * (ysqr + q4 * q4)
        yaw = math.atan2(t3, t4)
        return roll, pitch, yaw

    def madgwick(self, ax, ay, az, gx, gy, gz,
                 mx=None, my=None, mz=None, beta=0.5, dt=None):
        """One Madgwick update -> (roll, pitch, yaw) in radians."""
        deltat = self._dt(dt)
        q1, q2, q3, q4 = self.q
        usemag = mx is not None
        _2q1 = 2.0 * q1
        _2q2 = 2.0 * q2
        _2q3 = 2.0 * q3
        _2q4 = 2.0 * q4
        _2q1q3 = 2.0 * q1 * q3
        _2q3q4 = 2.0 * q3 * q4
        q1q1 = q1 * q1
        q1q2 = q1 * q2
        q1q3 = q1 * q3
        q1q4 = q1 * q4
        q2q2 = q2 * q2
        q2q3 = q2 * q3
        q2q4 = q2 * q4
        q3q3 = q3 * q3
        q3q4 = q3 * q4
        q4q4 = q4 * q4

        norm = math.sqrt(ax * ax + ay * ay + az * az)
        if norm == 0.0:
            return self._angles(q1, q2, q3, q4)
        norm = 1.0 / norm
        ax *= norm
        ay *= norm
        az *= norm

        if usemag:
            norm = math.sqrt(mx * mx + my * my + mz * mz)
            if norm == 0.0:
                return self._angles(q1, q2, q3, q4)
            norm = 1.0 / norm
            mx *= norm
            my *= norm
            mz *= norm
            _2q1mx = 2.0 * q1 * mx
            _2q1my = 2.0 * q1 * my
            _2q1mz = 2.0 * q1 * mz
            _2q2mx = 2.0 * q2 * mx
            hx = (mx * q1q1 - _2q1my * q4 + _2q1mz * q3 + mx * q2q2
                  + _2q2 * my * q3 + _2q2 * mz * q4 - mx * q3q3 - mx * q4q4)
            hy = (_2q1mx * q4 + my * q1q1 - _2q1mz * q2 + _2q2mx * q3
                  - my * q2q2 + my * q3q3 + _2q3 * mz * q4 - my * q4q4)
            _2bx = math.sqrt(hx * hx + hy * hy)
            _2bz = (-_2q1mx * q3 + _2q1my * q2 + mz * q1q1 + _2q2mx * q4
                    - mz * q2q2 + _2q3 * my * q4 - mz * q3q3 + mz * q4q4)
            _4bx = 2.0 * _2bx
            _4bz = 2.0 * _2bz
            s1 = (-_2q3 * (2.0 * q2q4 - _2q1q3 - ax) + _2q2 * (2.0 * q1q2 + _2q3q4 - ay)
                  - _2bz * q3 * (_2bx * (0.5 - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx)
                  + (-_2bx * q4 + _2bz * q2) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my)
                  + _2bx * q3 * (_2bx * (q1q3 + q2q4) + _2bz * (0.5 - q2q2 - q3q3) - mz))
            s2 = (_2q4 * (2.0 * q2q4 - _2q1q3 - ax) + _2q1 * (2.0 * q1q2 + _2q3q4 - ay)
                  - 4.0 * q2 * (1.0 - 2.0 * q2q2 - 2.0 * q3q3 - az)
                  + _2bz * q4 * (_2bx * (0.5 - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx)
                  + (_2bx * q3 + _2bz * q1) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my)
                  + (_2bx * q4 - _4bz * q2) * (_2bx * (q1q3 + q2q4) + _2bz * (0.5 - q2q2 - q3q3) - mz))
            s3 = (-_2q1 * (2.0 * q2q4 - _2q1q3 - ax) + _2q4 * (2.0 * q1q2 + _2q3q4 - ay)
                  - 4.0 * q3 * (1.0 - 2.0 * q2q2 - 2.0 * q3q3 - az)
                  + (-_4bx * q3 - _2bz * q1) * (_2bx * (0.5 - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx)
                  + (_2bx * q2 + _2bz * q4) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my)
                  + (_2bx * q1 - _4bz * q3) * (_2bx * (q1q3 + q2q4) + _2bz * (0.5 - q2q2 - q3q3) - mz))
            s4 = (_2q2 * (2.0 * q2q4 - _2q1q3 - ax) + _2q3 * (2.0 * q1q2 + _2q3q4 - ay)
                  + (-_4bx * q4 + _2bz * q2) * (_2bx * (0.5 - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx)
                  + (-_2bx * q1 + _2bz * q3) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my)
                  + _2bx * q2 * (_2bx * (q1q3 + q2q4) + _2bz * (0.5 - q2q2 - q3q3) - mz))
        else:
            _4q1 = 4.0 * q1
            _4q2 = 4.0 * q2
            _4q3 = 4.0 * q3
            _8q2 = 8.0 * q2
            _8q3 = 8.0 * q3
            s1 = _4q1 * q3q3 + _2q3 * ax + _4q1 * q2q2 - _2q2 * ay
            s2 = (_4q2 * q4q4 - _2q4 * ax + 4.0 * q1q1 * q2 - _2q1 * ay
                  - _4q2 + _8q2 * q2q2 + _8q2 * q3q3 + _4q2 * az)
            s3 = (4.0 * q1q1 * q3 + _2q1 * ax + _4q3 * q4q4 - _2q4 * ay
                  - _4q3 + _8q3 * q2q2 + _8q3 * q3q3 + _4q3 * az)
            s4 = 4.0 * q2q2 * q4 - _2q2 * ax + 4.0 * q3q3 * q4 - _2q3 * ay
        norm = math.sqrt(s1 * s1 + s2 * s2 + s3 * s3 + s4 * s4)
        if norm == 0.0:
            return self._angles(q1, q2, q3, q4)
        norm = 1.0 / norm
        s1 *= norm
        s2 *= norm
        s3 *= norm
        s4 *= norm

        qDot1 = 0.5 * (-q2 * gx - q3 * gy - q4 * gz) - beta * s1
        qDot2 = 0.5 * (q1 * gx + q3 * gz - q4 * gy) - beta * s2
        qDot3 = 0.5 * (q1 * gy - q2 * gz + q4 * gx) - beta * s3
        qDot4 = 0.5 * (q1 * gz + q2 * gy - q3 * gx) - beta * s4
        q1 += qDot1 * deltat
        q2 += qDot2 * deltat
        q3 += qDot3 * deltat
        q4 += qDot4 * deltat
        q1, q2, q3, q4 = self._store(q1, q2, q3, q4)
        return self._angles(q1, q2, q3, q4)

    def mahony(self, ax, ay, az, gx, gy, gz,
               mx=None, my=None, mz=None, kp=10.0, ki=0.0, dt=None):
        """One Mahony update -> (roll, pitch, yaw) in radians."""
        deltat = self._dt(dt)
        q1, q2, q3, q4 = self.q
        usemag = mx is not None
        q1q1 = q1 * q1
        q1q2 = q1 * q2
        q1q3 = q1 * q3
        q1q4 = q1 * q4
        q2q2 = q2 * q2
        q2q3 = q2 * q3
        q2q4 = q2 * q4
        q3q3 = q3 * q3
        q3q4 = q3 * q4
        q4q4 = q4 * q4

        norm = math.sqrt(ax * ax + ay * ay + az * az)
        if norm == 0.0:
            return self._angles(q1, q2, q3, q4)
        norm = 1.0 / norm
        ax *= norm
        ay *= norm
        az *= norm

        vx = 2.0 * (q2q4 - q1q3)
        vy = 2.0 * (q1q2 + q3q4)
        vz = q1q1 - q2q2 - q3q3 + q4q4
        ex = ay * vz - az * vy
        ey = az * vx - ax * vz
        ez = ax * vy - ay * vx

        if usemag:
            norm = math.sqrt(mx * mx + my * my + mz * mz)
            if norm == 0.0:
                return self._angles(q1, q2, q3, q4)
            norm = 1.0 / norm
            mx *= norm
            my *= norm
            mz *= norm
            hx = (2.0 * mx * (0.5 - q3q3 - q4q4) + 2.0 * my * (q2q3 - q1q4)
                  + 2.0 * mz * (q2q4 + q1q3))
            hy = (2.0 * mx * (q2q3 + q1q4) + 2.0 * my * (0.5 - q2q2 - q4q4)
                  + 2.0 * mz * (q3q4 - q1q2))
            bx = math.sqrt(hx * hx + hy * hy)
            bz = (2.0 * mx * (q2q4 - q1q3) + 2.0 * my * (q3q4 + q1q2)
                  + 2.0 * mz * (0.5 - q2q2 - q3q3))
            wx = 2.0 * bx * (0.5 - q3q3 - q4q4) + 2.0 * bz * (q2q4 - q1q3)
            wy = 2.0 * bx * (q2q3 - q1q4) + 2.0 * bz * (q1q2 + q3q4)
            wz = 2.0 * bx * (q1q3 + q2q4) + 2.0 * bz * (0.5 - q2q2 - q3q3)
            ex += my * wz - mz * wy
            ey += mz * wx - mx * wz
            ez += mx * wy - my * wx

        if ki > 0.0:
            self._eint[0] += ex
            self._eint[1] += ey
            self._eint[2] += ez
        else:
            self._eint[0] = 0.0
            self._eint[1] = 0.0
            self._eint[2] = 0.0

        gx = gx + kp * ex + ki * self._eint[0]
        gy = gy + kp * ey + ki * self._eint[1]
        gz = gz + kp * ez + ki * self._eint[2]

        pa = q2
        pb = q3
        pc = q4
        q1 = q1 + (-q2 * gx - q3 * gy - q4 * gz) * (0.5 * deltat)
        q2 = pa + (q1 * gx + pb * gz - pc * gy) * (0.5 * deltat)
        q3 = pb + (q1 * gy - pa * gz + pc * gx) * (0.5 * deltat)
        q4 = pc + (q1 * gz + pa * gy - pb * gx) * (0.5 * deltat)
        q1, q2, q3, q4 = self._store(q1, q2, q3, q4)
        return self._angles(q1, q2, q3, q4)
