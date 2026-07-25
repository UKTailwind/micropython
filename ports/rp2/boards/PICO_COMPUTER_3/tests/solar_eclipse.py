# solar_eclipse.py
#
# MicroPython port of solar_eclipse.bas (Micromite eXtreme version,
# March 14, 2017) - local circumstances of solar eclipses.
#
# Faithful transliteration: algorithms, evaluation order and even the
# quirks of the BASIC original are preserved so that the numeric output
# can be compared digit-for-digit with MMBasic:
#   - "pl = pl + 3.67446 + Sin(4*d - f)"  (BASIC line: '+' not '*')
#   - "Sin(2*ju - 5*l + l + ...)"
#   - MMBasic's integer MOD semantics in nut2000_lp
# Runs on MicroPython (double-precision build) and CPython.

from math import sin, cos, tan, asin, acos, atan, sqrt, pi, floor, trunc

try:
    from time import ticks_ms, ticks_diff
except ImportError:
    from time import monotonic

    def ticks_ms():
        return int(monotonic() * 1000.0)

    def ticks_diff(a, b):
        return a - b

# global constants

pi2 = 2.0 * pi
pidiv2 = 0.5 * pi
dtr = pi / 180.0
rtd = 180.0 / pi
atr = pi / 648000.0
seccon = 206264.8062470964

# equatorial radius of the earth (kilometers)
reqm = 6378.14

# earth flattening factor (nd)
flat = 1.0 / 298.257

# astronomical unit (kilometers)
aunit = 149597870.691

# radius of the sun (kilometers)
radsun = 696000.0

# radius of the moon (kilometers)
radmoon = 1738.0

# solar ephemeris data: (xsl, xsr, xsa, xsb)

XS = (
    (403406,      0, 4.721964,      1.621043),
    (195207, -97597, 5.937458,  62830.348067),
    (119433, -59715, 1.115589,  62830.821524),
    (112392, -56188, 5.781616,  62829.634302),
    (  3891,  -1556, 5.5474,   125660.5691),
    (  2819,  -1126, 1.5120,   125660.9845),
    (  1721,   -861, 4.1897,    62832.4766),
    (     0,    941, 1.163,         0.813),
    (   660,   -264, 5.415,    125659.310),
    (   350,   -163, 4.315,     57533.850),
    (   334,      0, 4.553,       -33.931),
    (   314,    309, 5.198,    777137.715),
    (   268,   -158, 5.989,     78604.191),
    (   242,      0, 2.911,         5.412),
    (   234,    -54, 1.423,     39302.098),
    (   158,      0, 0.061,       -34.861),
    (   132,    -93, 2.317,    115067.698),
    (   129,    -20, 3.193,     15774.337),
    (   114,      0, 2.828,      5296.670),
    (    99,    -47, 0.52,      58849.27),
    (    93,      0, 4.65,       5296.11),
    (    86,      0, 4.35,      -3980.70),
    (    78,    -33, 2.75,      52237.69),
    (    72,    -32, 4.50,      55076.47),
    (    68,      0, 3.23,        261.08),
    (    64,    -10, 1.22,      15773.85),
    (    46,    -16, 0.14,     188491.03),
    (    38,      0, 3.44,      -7756.55),
    (    37,      0, 4.37,        264.89),
    (    32,    -24, 1.14,     117906.27),
    (    29,    -13, 2.84,      55075.75),
    (    28,      0, 5.96,      -7961.39),
    (    27,     -9, 5.09,     188489.81),
    (    27,      0, 1.72,       2132.19),
    (    25,    -17, 2.56,     109771.03),
    (    24,    -11, 1.92,      54868.56),
    (    21,      0, 0.09,      25443.93),
    (    21,     31, 5.98,     -55731.43),
    (    20,    -10, 4.03,      60697.74),
    (    18,      0, 4.27,       2132.79),
    (    17,    -12, 0.79,     109771.63),
    (    14,      0, 4.24,      -7752.82),
    (    13,     -5, 2.01,     188491.91),
    (    13,      0, 2.65,        207.81),
    (    13,      0, 4.98,      29424.63),
    (    12,      0, 0.93,         -7.99),
    (    10,      0, 2.21,      46941.14),
    (    10,      0, 3.59,        -68.29),
    (    10,      0, 1.50,      21463.25),
    (    10,     -9, 2.55,     157208.40),
)

# subset of IAU2000 nutation data (11 values per row, 13 rows)

XNUT = (
    ( 0,  0, 0,  0, 1, -172064161, -174666, 92052331,  9086,  33386, 15377),
    ( 0,  0, 2, -2, 2,  -13170906,   -1675,  5730336, -3015, -13696, -4587),
    ( 0,  0, 2,  0, 2,   -2276413,    -234,   978459,  -485,   2796,  1374),
    ( 0,  0, 0,  0, 2,    2074554,     207,  -897492,   470,   -698,  -291),
    ( 0,  1, 0,  0, 0,    1475877,   -3633,    73871,  -184,  11817, -1924),
    ( 0,  1, 2, -2, 2,    -516821,    1226,   224386,  -677,   -524,  -174),
    ( 1,  0, 0,  0, 0,     711159,      73,    -6750,     0,   -872,   358),
    ( 0,  0, 2,  0, 1,    -387298,    -367,   200728,    18,    380,   318),
    ( 1,  0, 2,  0, 2,    -301461,     -36,   129025,   -63,    816,   367),
    ( 0, -1, 2, -2, 2,     215829,    -494,   -95929,   299,    111,   132),
    ( 0,  0, 2, -2, 1,     128227,     137,   -68982,    -9,    181,    39),
    (-1,  0, 2,  0, 2,     123457,      11,   -53311,    32,     19,    -4),
    (-1,  0, 0,  2, 0,     156994,      10,    -1235,     0,   -168,    82),
)

# leap second data: (jdleap, leapsec)

LEAP = (
    (2441317.5, 10.0), (2441499.5, 11.0), (2441683.5, 12.0), (2442048.5, 13.0),
    (2442413.5, 14.0), (2442778.5, 15.0), (2443144.5, 16.0), (2443509.5, 17.0),
    (2443874.5, 18.0), (2444239.5, 19.0), (2444786.5, 20.0), (2445151.5, 21.0),
    (2445516.5, 22.0), (2446247.5, 23.0), (2447161.5, 24.0), (2447892.5, 25.0),
    (2448257.5, 26.0), (2448804.5, 27.0), (2449169.5, 28.0), (2449534.5, 29.0),
    (2450083.5, 30.0), (2450630.5, 31.0), (2451179.5, 32.0), (2453736.5, 33.0),
    (2454832.5, 34.0), (2456109.5, 35.0), (2457204.5, 36.0), (2457754.5, 37.0),
)

MONTHS = ("January", "February", "March", "April", "May", "June", "July",
          "August", "September", "October", "November", "December")

# globals (as in the BASIC original)

jdtdbi = 0.0
jdsaved = 0.0
jdprint = 0.0
trr = 0.0
elev_minima = 0.0
obslat = 0.0
obslong = 0.0
obsalt = 0.0


def sgn(x):
    if x > 0.0:
        return 1
    if x < 0.0:
        return -1
    return 0


def mmod(a, b):
    # MMBasic MOD operator: remainder of an integer division
    # (operands converted to 64-bit integers, C-style sign)
    ia = int(a + 0.5) if a >= 0.0 else -int(0.5 - a)
    ib = int(b + 0.5) if b >= 0.0 else -int(0.5 - b)
    r = abs(ia) % abs(ib)
    return float(-r if ia < 0 else r)


def modulo(x):
    # modulo 2 pi function
    a = x - pi2 * trunc(x / pi2)
    if a < 0.0:
        a = a + pi2
    return a


def atan3(a, b):
    # four quadrant inverse tangent (0 <= atan3 <= 2*pi)
    if abs(a) < 1.0e-10:
        return (1.0 - sgn(b)) * pidiv2
    c = (2.0 - sgn(a)) * pidiv2
    if abs(b) < 1.0e-10:
        return c
    return c + sgn(a) * sgn(b) * (abs(atan(a / b)) - pidiv2)


def vecmag(a):
    return sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def uvector(a):
    amag = vecmag(a)
    if amag != 0.0:
        return (a[0] / amag, a[1] / amag, a[2] / amag)
    return (0.0, 0.0, 0.0)


def nut2000_lp(jdate):
    # low precision nutation based on iau 2000a (largest 13 terms)
    # returns (dpsi, deps) in arcseconds

    rev = 360.0 * 3600.0

    t = (jdate - 2451545.0) / 36525.0

    # fundamental (delaunay) arguments from simon et al. (1994)
    # NB: MMBasic's Mod binds like '*', so the leading constant is
    # added after the (integer) Mod - replicated here via mmod()

    el = (485868.249036 + mmod(t * (1717915923.2178 + t * (31.8792 + t * (0.051635 + t * (-0.00024470)))), rev)) / seccon

    elp = (1287104.79305 + mmod(t * (129596581.0481 + t * (-0.5532 + t * (0.000136 + t * (-0.00001149)))), rev)) / seccon

    f = (335779.526232 + mmod(t * (1739527262.8478 + t * (-12.7512 + t * (-0.001037 + t * (0.00000417)))), rev)) / seccon

    d = (1072260.70369 + mmod(t * (1602961601.2090 + t * (-6.3706 + t * (0.006593 + t * (-0.00003169)))), rev)) / seccon

    omega = (450160.398036 + mmod(t * (-6962890.5431 + t * (7.4722 + t * (0.007702 + t * (-0.00005939)))), rev)) / seccon

    dpsi = 0.0
    deps = 0.0

    # sum nutation series terms (i = 13 down to 1)

    for i in range(12, -1, -1):
        x = XNUT[i]
        arg = x[0] * el + x[1] * elp + x[2] * f + x[3] * d + x[4] * omega
        dpsi = (x[5] + x[6] * t) * sin(arg) + x[9] * cos(arg) + dpsi
        deps = (x[7] + x[8] * t) * cos(arg) + x[10] * sin(arg) + deps

    dpsi = 1.0e-7 * dpsi
    deps = 1.0e-7 * deps

    # out-of-phase component of principal (18.6-year) term

    dpsi = dpsi + 0.0033 * cos(omega)
    deps = deps + 0.0015 * sin(omega)

    return dpsi, deps


def obliq_lp(jdate):
    # nutations and true obliquity -> (dpsi, deps, obliq) in radians

    tjdh = floor(jdate)
    tjdl = jdate - tjdh

    th = (tjdh - 2451545.0) / 36525.0
    tl = tjdl / 36525.0

    t = th + tl
    t2 = t * t
    t3 = t2 * t

    dpsi, deps = nut2000_lp(jdate)

    obm = 84381.4480 - 46.8150 * t - 0.00059 * t2 + 0.001813 * t3

    obt = obm + deps

    return atr * dpsi, atr * deps, atr * obt


def gast2(jdate):
    # greenwich apparent sidereal time (radians)

    tjdh = floor(jdate)
    tjdl = jdate - tjdh

    th = (tjdh - 2451545.0) / 36525.0
    tl = tjdl / 36525.0

    t = th + tl
    t2 = t * t
    t3 = t2 * t

    dpsi, deps = nut2000_lp(jdate)

    obm = 84381.4480 - 46.8150 * t - 0.00059 * t2 + 0.001813 * t3

    obliq = obm + deps

    eqeq = (dpsi / 15.0) * cos(obliq / seccon)

    st = eqeq - 6.2e-6 * t3 + 0.093104 * t2 + 67310.54841 + 8640184.812866 * tl + 3155760000.0 * tl + 8640184.812866 * th + 3155760000.0 * th

    x = st / 3600.0

    gast = x - 24.0 * trunc(x / 24.0)

    if gast < 0.0:
        gast = gast + 24.0

    return pi2 * (gast / 24.0)


def sun(jd):
    # precision ephemeris of the Sun -> eci position vector (km)

    u = (jd - 2451545.0) / 3652500.0

    # nutation in longitude

    a1 = 2.18 + u * (-3375.7 + u * 0.36)
    a2 = 3.51 + u * (125666.39 + u * 0.1)

    psi = 0.0000001 * (-834.0 * sin(a1) - 64.0 * sin(a2))

    # nutation in obliquity

    deps = 0.0000001 * u * (-226938 + u * (-75 + u * (96926 + u * (-2491 - u * 12104))))

    meps = 0.0000001 * (4090928.0 + 446.0 * cos(a1) + 28.0 * cos(a2))

    eps = meps + deps

    seps = sin(eps)
    ceps = cos(eps)

    dl = 0.0
    dr = 0.0

    for xl, xr, xa, xb in XS:
        w = xa + xb * u
        dl = dl + xl * sin(w)
        if xr != 0.0:
            dr = dr + xr * cos(w)

    dl = modulo(dl * 0.0000001 + 4.9353929 + 62833.196168 * u)

    dr = 149597870.691 * (dr * 0.0000001 + 1.0001026)

    rlsun = modulo(dl + 0.0000001 * (-993.0 + 17.0 * cos(3.1 + 62830.14 * u)) + psi)

    rb = 0.0

    # geocentric declination and right ascension

    crl = cos(rlsun)
    srl = sin(rlsun)
    crb = cos(rb)
    srb = sin(rb)

    decl = asin(ceps * srb + seps * crb * srl)

    sra = -seps * srb + ceps * crb * srl
    cra = crb * crl

    rasc = atan3(sra, cra)

    return (dr * cos(rasc) * cos(decl),
            dr * sin(rasc) * cos(decl),
            dr * sin(decl))


def moon(jdate):
    # geocentric position of the moon -> eci position vector (km)

    dpsi, deps, obliq = obliq_lp(jdate)

    t1 = (jdate - 2451545.0) / 36525.0

    t2 = t1 * t1
    t3 = t1 * t1 * t1
    t4 = t1 * t1 * t1 * t1

    # fundamental arguments (radians)

    ll = dtr * (218 + (18 * 60 + 59.95571) / 3600)
    ll = modulo(ll + atr * (1732564372.83264 * t1 - 4.7763 * t2 + .006681 * t3 - 0.00005522 * t4))

    d = dtr * (297 + (51 * 60 + .73512) / 3600)
    d = modulo(d + atr * (1602961601.4603 * t1 - 5.8681 * t2 + .006595 * t3 - 0.00003184 * t4))

    lp = dtr * (357 + (31 * 60 + 44.79306) / 3600)
    lp = modulo(lp + atr * (129596581.0474 * t1 - .5529 * t2 + 0.000147 * t3))

    l = dtr * (134 + (57 * 60 + 48.28096) / 3600)
    l = modulo(l + atr * (1717915923.4728 * t1 + 32.3893 * t2 + .051651 * t3 - 0.0002447 * t4))

    f = dtr * (93 + (16 * 60 + 19.55755) / 3600)
    f = modulo(f + atr * (1739527263.0983 * t1 - 12.2505 * t2 - .001021 * t3 + 0.00000417 * t4))

    t = dtr * (100 + (27 * 60 + 59.22059) / 3600)
    t = modulo(t + atr * (129597742.2758 * t1 - .0202 * t2 + .000009 * t3 + 0.00000015 * t4))

    ve = dtr * (181 + (58 * 60 + 47.28305) / 3600)
    ve = modulo(ve + atr * 210664136.43355 * t1)

    ma = dtr * (355 + (25 * 60 + 59.78866) / 3600)
    ma = modulo(ma + atr * 68905077.59284 * t1)

    ju = dtr * (34 + (21 * 60 + 5.34212) / 3600)
    ju = modulo(ju + atr * 10925660.42861 * t1)

    # geocentric distance (kilometers)

    # a(c,0,r) series

    rm = 385000.52899
    rm = rm - 20905.35504 * sin(l + pidiv2)
    rm = rm - 3699.11092 * sin(2 * d - l + pidiv2)
    rm = rm - 2955.96756 * sin(2 * d + pidiv2)
    rm = rm - 569.92512 * sin(2 * l + pidiv2)
    rm = rm + 246.15848 * sin(2 * d - 2 * l + pidiv2)
    rm = rm - 204.58598 * sin(2 * d - lp + pidiv2)
    rm = rm - 170.73308 * sin(2 * d + l + pidiv2)
    rm = rm - 152.13771 * sin(2 * d - lp - l + pidiv2)
    rm = rm - 129.62014 * sin(lp - l + pidiv2)
    rm = rm + 108.7427 * sin(d + pidiv2)
    rm = rm + 104.75523 * sin(lp + l + pidiv2)
    rm = rm + 79.66056 * sin(l - 2 * f + pidiv2)
    rm = rm + 48.8883 * sin(lp + pidiv2)
    rm = rm - 34.78252 * sin(4 * d - l + pidiv2)
    rm = rm + 30.82384 * sin(2 * d + lp + pidiv2)
    rm = rm + 24.20848 * sin(2 * d + lp - l + pidiv2)
    rm = rm - 23.21043 * sin(3 * l + pidiv2)
    rm = rm - 21.63634 * sin(4 * d - 2 * l + pidiv2)
    rm = rm - 16.67471 * sin(d + lp + pidiv2)
    rm = rm + 14.40269 * sin(2 * d - 3 * l + pidiv2)
    rm = rm - 12.8314 * sin(2 * d - lp + l + pidiv2)
    rm = rm - 11.64995 * sin(4 * d + pidiv2)
    rm = rm - 10.44476 * sin(2 * d + 2 * l + pidiv2)
    rm = rm + 10.32111 * sin(2 * d - 2 * f + pidiv2)
    rm = rm + 10.0562 * sin(2 * d - lp - 2 * l + pidiv2)
    rm = rm - 9.88445 * sin(2 * d - 2 * lp + pidiv2)
    rm = rm + 8.75156 * sin(2 * d - l - 2 * f + pidiv2)
    rm = rm - 8.37911 * sin(d - l + pidiv2)
    rm = rm - 7.00269 * sin(lp - 2 * l + pidiv2)
    rm = rm + 6.322 * sin(d + l + pidiv2)
    rm = rm + 5.75085 * sin(lp + 2 * l + pidiv2)
    rm = rm - 4.95013 * sin(2 * d - 2 * lp - l + pidiv2)
    rm = rm - 4.42118 * sin(2 * l - 2 * f + pidiv2)
    rm = rm + 4.13111 * sin(2 * d + l - 2 * f + pidiv2)
    rm = rm - 3.95798 * sin(4 * d - lp - l + pidiv2)
    rm = rm + 3.25824 * sin(3 * d - l + pidiv2)
    rm = rm - 3.1483 * sin(2 * f + pidiv2)
    rm = rm + 2.61641 * sin(2 * d + lp + l + pidiv2)
    rm = rm + 2.35363 * sin(2 * d + 2 * lp - l + pidiv2)
    rm = rm - 2.11713 * sin(2 * lp - l + pidiv2)
    rm = rm - 1.89704 * sin(4 * d - lp - 2 * l + pidiv2)
    rm = rm - 1.73853 * sin(d - 2 * l + pidiv2)
    rm = rm - 1.57139 * sin(4 * d - lp + pidiv2)
    rm = rm - 1.42255 * sin(4 * d + l + pidiv2)
    rm = rm - 1.41893 * sin(3 * d + pidiv2)
    rm = rm + 1.16553 * sin(2 * lp + l + pidiv2)
    rm = rm - 1.11694 * sin(4 * l + pidiv2)
    rm = rm + 1.06567 * sin(2 * lp + pidiv2)
    rm = rm - .93332 * sin(d + lp + l + pidiv2)
    rm = rm + .86243 * sin(3 * d - 2 * l + pidiv2)
    rm = rm + .85124 * sin(d + lp - l + pidiv2)
    rm = rm - .8488 * sin(2 * d - lp + 2 * l + pidiv2)
    rm = rm - .79563 * sin(d - 2 * f + pidiv2)
    rm = rm + .77854 * sin(2 * d - 4 * l + pidiv2)
    rm = rm + .77404 * sin(2 * d - 2 * l + 2 * f + pidiv2)
    rm = rm - .66968 * sin(2 * d + 3 * l + pidiv2)
    rm = rm - .65753 * sin(2 * d - 2 * lp + l + pidiv2)
    rm = rm + .65706 * sin(2 * d - lp - 2 * f + pidiv2)
    rm = rm + .59632 * sin(2 * d - l + 2 * f + pidiv2)
    rm = rm + .57879 * sin(4 * d + lp - l + pidiv2)
    rm = rm - .51423 * sin(4 * d - 3 * l + pidiv2)
    rm = rm - .50792 * sin(4 * d - 2 * f + pidiv2)
    rm = rm + .49755 * sin(d - lp + pidiv2)
    rm = rm + .49504 * sin(2 * d - lp - 3 * l + pidiv2)
    rm = rm + .47262 * sin(2 * d - 2 * l - 2 * f + pidiv2)
    rm = rm - .4225 * sin(6 * d - 2 * l + pidiv2)
    rm = rm - .42241 * sin(lp - 3 * l + pidiv2)
    rm = rm - .41071 * sin(2 * d - 3 * lp + pidiv2)
    rm = rm + .37852 * sin(d + 2 * l + pidiv2)
    rm = rm + .35508 * sin(lp + 3 * l + pidiv2)
    rm = rm + .34302 * sin(2 * d - 2 * lp - 2 * l + pidiv2)
    rm = rm + .33463 * sin(lp - l + 2 * f + pidiv2)
    rm = rm + .33225 * sin(d + lp - 2 * l + pidiv2)
    rm = rm + .32334 * sin(2 * d - lp - l - 2 * f + pidiv2)
    rm = rm - .32176 * sin(4 * d - l - 2 * f + pidiv2)
    rm = rm - .28663 * sin(6 * d - l + pidiv2)
    rm = rm + .28399 * sin(2 * d + 2 * l - 2 * f + pidiv2)
    rm = rm - .27904 * sin(4 * d - 2 * lp - l + pidiv2)
    rm = rm + .2556 * sin(3 * d - lp - l + pidiv2)
    rm = rm - .2481 * sin(lp + l - 2 * f + pidiv2)
    rm = rm + .24452 * sin(4 * d + lp + pidiv2)
    rm = rm + .23695 * sin(4 * d + lp - 2 * l + pidiv2)
    rm = rm - .21258 * sin(3 * d + lp - l + pidiv2)
    rm = rm + .21251 * sin(2 * d + lp + 2 * l + pidiv2)
    rm = rm + .20941 * sin(2 * d - lp + l - 2 * f + pidiv2)
    rm = rm - .20285 * sin(4 * d - lp + l + pidiv2)
    rm = rm + .20099 * sin(3 * d - 2 * f + pidiv2)
    rm = rm - .18567 * sin(lp - 2 * f + pidiv2)
    rm = rm - .18316 * sin(6 * d - 3 * l + pidiv2)
    rm = rm + .16857 * sin(2 * d + lp - 3 * l + pidiv2)
    rm = rm - .15802 * sin(lp + 2 * f + pidiv2)
    rm = rm - .15707 * sin(3 * d - lp + pidiv2)
    rm = rm - .14806 * sin(2 * d - 3 * lp - l + pidiv2)
    rm = rm + .14763 * sin(2 * d + 2 * lp + pidiv2)
    rm = rm + .14368 * sin(2 * d + lp - 2 * l + pidiv2)
    rm = rm - .13922 * sin(4 * d + 2 * l + pidiv2)
    rm = rm - .13617 * sin(2 * lp - 2 * l + pidiv2)
    rm = rm - .13571 * sin(2 * d + lp - 2 * f + pidiv2)
    rm = rm - .12805 * sin(4 * d - 2 * lp + pidiv2)
    rm = rm + .11411 * sin(d - lp - l + pidiv2)
    rm = rm + .10998 * sin(d - lp + l + pidiv2)
    rm = rm - .10887 * sin(2 * d + 2 * lp - 2 * l + pidiv2)
    rm = rm - .10833 * sin(4 * d - 2 * lp - 2 * l + pidiv2)
    rm = rm - .10766 * sin(3 * d + lp + pidiv2)
    rm = rm - .10326 * sin(l + 2 * f + pidiv2)
    rm = rm - .09938 * sin(d - 3 * l + pidiv2)
    rm = rm - .08587 * sin(6 * d + pidiv2)
    rm = rm - .07982 * sin(4 * d - 2 * l - 2 * f + pidiv2)
    rm = rm - 6.678e-02 * sin(6 * d - lp - 2 * l + pidiv2)
    rm = rm - 6.545e-02 * sin(3 * d + l + pidiv2)
    rm = rm + .06055 * sin(d + l - 2 * f + pidiv2)
    rm = rm - .05904 * sin(d + lp + 2 * l + pidiv2)
    rm = rm - .05888 * sin(5 * l + pidiv2)
    rm = rm - .0585 * sin(2 * d - lp + 3 * l + pidiv2)
    rm = rm - .05789 * sin(4 * d - lp - 2 * f + pidiv2)
    rm = rm - .05527 * sin(2 * d + lp + l - 2 * f + pidiv2)
    rm = rm + .05293 * sin(3 * d - lp - 2 * l + pidiv2)
    rm = rm - .05191 * sin(6 * d - lp - l + pidiv2)
    rm = rm + .05072 * sin(2 * lp + 2 * l + pidiv2)
    rm = rm - .0502 * sin(lp - 2 * l + 2 * f + pidiv2)
    rm = rm - .04843 * sin(3 * d - 3 * l + pidiv2)
    rm = rm + .0474 * sin(2 * d - 5 * l + pidiv2)
    rm = rm - .04736 * sin(2 * d + lp - l - 2 * f + pidiv2)
    rm = rm - .04608 * sin(2 * d - 2 * lp + 2 * l + pidiv2)
    rm = rm + .04591 * sin(5 * d - 2 * l + pidiv2)
    rm = rm - .04422 * sin(2 * d + 4 * l + pidiv2)
    rm = rm - .04316 * sin(4 * d - lp - 3 * l + pidiv2)
    rm = rm - .04232 * sin(d - l - 2 * f + pidiv2)
    rm = rm - .03894 * sin(3 * lp - l + pidiv2)
    rm = rm + .0381 * sin(3 * d + lp - 2 * l + pidiv2)
    rm = rm + .03734 * sin(2 * d - lp - l + 2 * f + pidiv2)
    rm = rm + .03729 * sin(d + 2 * lp + pidiv2)
    rm = rm + .03682 * sin(4 * d + lp + l + pidiv2)
    rm = rm + .03379 * sin(d + lp - 2 * f + pidiv2)
    rm = rm + .03265 * sin(lp + 2 * l - 2 * f + pidiv2)
    rm = rm + .03143 * sin(2 * d + 2 * f + pidiv2)
    rm = rm + .03024 * sin(2 * d - lp - 2 * l + 2 * f + pidiv2)
    rm = rm - .02948 * sin(d - 2 * lp + pidiv2)
    rm = rm - .02939 * sin(4 * d - 4 * l + pidiv2)
    rm = rm + .0291 * sin(2 * d - 3 * l - 2 * f + pidiv2)
    rm = rm - .02855 * sin(2 * d - 3 * lp + l + pidiv2)
    rm = rm + .02839 * sin(2 * d - 2 * lp - 2 * f + pidiv2)
    rm = rm - .02698 * sin(4 * d - lp - l - 2 * f + pidiv2)
    rm = rm - .02674 * sin(lp - 4 * l + pidiv2)
    rm = rm + .02658 * sin(4 * d + 2 * lp - 2 * l + pidiv2)
    rm = rm - .02471 * sin(d - l + 2 * f + pidiv2)
    rm = rm - .02436 * sin(6 * d - lp - 3 * l + pidiv2)
    rm = rm - .02399 * sin(4 * d + lp - 3 * l + pidiv2)
    rm = rm + .02368 * sin(d + 3 * l + pidiv2)
    rm = rm + .02334 * sin(2 * d - lp - 4 * l + pidiv2)
    rm = rm + .02304 * sin(lp + 4 * l + pidiv2)
    rm = rm + .02127 * sin(3 * lp + pidiv2)
    rm = rm - .02079 * sin(4 * d - lp + 2 * l + pidiv2)
    rm = rm - .02008 * sin(2 * d - 3 * l + 2 * f + pidiv2)

    # a(p,0,r) series

    rm = rm + 1.0587 * sin(2 * t - 2 * ju + 2 * d - l + 90.11969000000001 * dtr)
    rm = rm + .72783 * sin(18 * ve - 16 * t - 2 * l + 116.54311 * dtr)
    rm = rm + .68256 * sin(18 * ve - 16 * t + 296.54574 * dtr)
    rm = rm + .59827 * sin(3 * ve - 3 * t + 2 * d - l + 89.98187 * dtr)
    rm = rm + .45648 * sin(ll + l - f + 270.00126 * dtr)
    rm = rm + .45276 * sin(ll - l - f + 90.00128 * dtr)
    rm = rm + .41011 * sin(2 * t - 3 * ju + 2 * d - l + 280.06924 * dtr)
    rm = rm + .20497 * sin(t - ju - 2 * d + 91.79862 * dtr)
    rm = rm + .20473 * sin(18 * ve - 16 * t - 2 * d - l + 116.54222 * dtr)
    rm = rm + .20367 * sin(18 * ve - 16 * t + 2 * d - l + 296.54299 * dtr)
    rm = rm + .16644 * sin(2 * ve - 2 * t - 2 * d + 90.36386 * dtr)
    rm = rm + .1578 * sin(4 * t - 8 * ma + 3 * ju + l + 194.98833 * dtr)
    rm = rm + .1578 * sin(4 * t - 8 * ma + 3 * ju - l + 14.98841 * dtr)
    rm = rm + .15751 * sin(t - ju - 2 * d + l + 91.74578 * dtr)
    rm = rm + .1445 * sin(2 * t - 2 * ju - l + 89.97863 * dtr)
    rm = rm + .13811 * sin(ve - t - l + 270.00993 * dtr)
    rm = rm + .13477 * sin(18 * ve - 16 * t - 2 * d + 116.53978 * dtr)
    rm = rm + .12671 * sin(18 * ve - 16 * t + 2 * d - 2 * l + 296.54238 * dtr)
    rm = rm + .12666 * sin(t - ju - l + 91.22751 * dtr)
    rm = rm + .12362 * sin(ve - t - 2 * d + 269.98523 * dtr)
    rm = rm + .12047 * sin(2 * ve - 2 * t + 2 * d - l + 269.99692 * dtr)
    rm = rm + .11998 * sin(ve - t + l + 90.01606 * dtr)
    rm = rm + .11617 * sin(2 * ve - 2 * t - 2 * d + l + 90.31081 * dtr)
    rm = rm + .11256 * sin(4 * t - 8 * ma + 3 * ju + 2 * d - l + 197.11421 * dtr)
    rm = rm + .11251 * sin(4 * t - 8 * ma + 3 * ju - 2 * d + l + 17.11263 * dtr)
    rm = rm + .11226 * sin(4 * t - 8 * ma + 3 * ju + 2 * d + 196.69224 * dtr)
    rm = rm + .11216 * sin(4 * t - 8 * ma + 3 * ju - 2 * d + 16.68897 * dtr)
    rm = rm + .10689 * sin(ll + 2 * d - f + 270.00092 * dtr)
    rm = rm + .10504 * sin(t - ju + l + 271.06726 * dtr)
    rm = rm + .1006 * sin(ve - t - 2 * d + l + 269.98452 * dtr)
    rm = rm + 9.932e-02 * sin(3 * ve - 3 * t - l + 90.1054 * dtr)
    rm = rm + .09554 * sin(ll - 2 * d - f + 90.00096 * dtr)
    rm = rm + .08508 * sin(ll - f + 270.00061 * dtr)
    rm = rm + 7.9450e-02 * sin(4 * ve - 4 * t + 2 * d - l + 89.99224 * dtr)
    rm = rm + .07725 * sin(2 * t - 3 * ju - l + 280.16516 * dtr)
    rm = rm + 7.054e-02 * sin(6 * ve - 8 * t + 2 * d - l + 77.22087 * dtr)
    rm = rm + 6.313e-02 * sin(2 * ju - 5 * l + l + 256.56163 * dtr)

    # a(p,1,r) series

    rm = rm + .51395 * t1 * sin(2 * d - lp + pidiv2)
    rm = rm + .38245 * t1 * sin(2 * d - lp - l + pidiv2)
    rm = rm + .32654 * t1 * sin(lp - l + pidiv2)
    rm = rm + .26396 * t1 * sin(lp + l + 270 * dtr)
    rm = rm + .12302 * t1 * sin(lp + 270 * dtr)
    rm = rm + .07754 * t1 * sin(2 * d + lp + 270 * dtr)
    rm = rm + .06068 * t1 * sin(2 * d + lp - l + 270 * dtr)
    rm = rm + .0497 * t1 * sin(2 * d - 2 * lp + pidiv2)
    rm = rm + .04194 * t1 * sin(d + lp + pidiv2)
    rm = rm + .03222 * t1 * sin(2 * d - lp + l + pidiv2)
    rm = rm + .02529 * t1 * sin(2 * d - lp - 2 * l + 270 * dtr)
    rm = rm + .0249 * t1 * sin(2 * d - 2 * lp - l + pidiv2)
    rm = rm + .00149 * t2 * sin(2 * d - lp + pidiv2)
    rm = rm + .00111 * t2 * sin(2 * d - lp - l + pidiv2)

    rmm = rm

    # geocentric ecliptic longitude (arc seconds)

    # a(c,0,v) series

    dv = 22639.58578 * sin(l)
    dv = dv + 4586.4383 * sin(2 * d - l)
    dv = dv + 2369.91394 * sin(2 * d)
    dv = dv + 769.02571 * sin(2 * l)
    dv = dv - 666.4171 * sin(lp)
    dv = dv - 411.59567 * sin(2 * f)
    dv = dv + 211.65555 * sin(2 * d - 2 * l)
    dv = dv + 205.43582 * sin(2 * d - lp - l)
    dv = dv + 191.9562 * sin(2 * d + l)
    dv = dv + 164.72851 * sin(2 * d - lp)
    dv = dv - 147.32129 * sin(lp - l)
    dv = dv - 124.98812 * sin(d)
    dv = dv - 109.38029 * sin(lp + l)
    dv = dv + 55.17705 * sin(2 * d - 2 * f)
    dv = dv - 45.0996 * sin(l + 2 * f)
    dv = dv + 39.53329 * sin(l - 2 * f)
    dv = dv + 38.42983 * sin(4 * d - l)
    dv = dv + 36.12381 * sin(3 * l)
    dv = dv + 30.77257 * sin(4 * d - 2 * l)
    dv = dv - 28.39708 * sin(2 * d + lp - l)
    dv = dv - 24.35821 * sin(2 * d + lp)
    dv = dv - 18.58471 * sin(d - l)
    dv = dv + 17.95446 * sin(d + lp)
    dv = dv + 14.53027 * sin(2 * d - lp + l)
    dv = dv + 14.3797 * sin(2 * d + 2 * l)
    dv = dv + 13.89906 * sin(4 * d)
    dv = dv + 13.19406 * sin(2 * d - 3 * l)
    dv = dv - 9.67905 * sin(lp - 2 * l)
    dv = dv - 9.36586 * sin(2 * d - l + 2 * f)
    dv = dv + 8.60553 * sin(2 * d - lp - 2 * l)
    dv = dv - 8.45310 * sin(d + l)
    dv = dv + 8.05016 * sin(2 * d - 2 * lp)
    dv = dv - 7.63015 * sin(lp + 2 * l)
    dv = dv - 7.44749 * sin(2 * lp)
    dv = dv + 7.37119 * sin(2 * d - 2 * lp - l)
    dv = dv - 6.38315 * sin(2 * d + l - 2 * f)
    dv = dv - 5.74161 * sin(2 * d + 2 * f)
    dv = dv + 4.37401 * sin(4 * d - lp - l)
    dv = dv - 3.99761 * sin(2 * l + 2 * f)
    dv = dv - 3.20969 * sin(3 * d - l)
    dv = dv - 2.91454 * sin(2 * d + lp + l)
    dv = dv + 2.73189 * sin(4 * d - lp - 2 * l)
    dv = dv - 2.56794 * sin(2 * lp - l)
    dv = dv - 2.5212 * sin(2 * d + 2 * lp - l)
    dv = dv + 2.48889 * sin(2 * d + lp - 2 * l)
    dv = dv + 2.14607 * sin(2 * d - lp - 2 * f)
    dv = dv + 1.97773 * sin(4 * d + l)
    dv = dv + 1.93368 * sin(4 * l)
    dv = dv + 1.87076 * sin(4 * d - lp)
    dv = dv - 1.75297 * sin(d - 2 * l)
    dv = dv - 1.43716 * sin(2 * d + lp - 2 * f)
    dv = dv - 1.37257 * sin(2 * l - 2 * f)
    dv = dv + 1.26182 * sin(d + lp + l)
    dv = dv - 1.22412 * sin(3 * d - 2 * l)
    dv = dv + 1.18683 * sin(4 * d - 3 * l)
    dv = dv + 1.177 * sin(2 * d - lp + 2 * l)
    dv = dv - 1.16169 * sin(2 * lp + l)
    dv = dv + 1.07769 * sin(d + lp - l)
    dv = dv + 1.0595 * sin(2 * d + 3 * l)
    dv = dv - .99022 * sin(2 * d + l + 2 * f)
    dv = dv + .94828 * sin(2 * d - 4 * l)
    dv = dv + .75168 * sin(2 * d - 2 * lp + l)
    dv = dv - .66938 * sin(lp - 3 * l)
    dv = dv - .63521 * sin(4 * d + lp - l)
    dv = dv - .58399 * sin(d + 2 * l)
    dv = dv - .58331 * sin(d - 2 * f)
    dv = dv + .57156 * sin(6 * d - 2 * l)
    dv = dv - .56064 * sin(2 * d - 2 * l - 2 * f)
    dv = dv - .55692 * sin(d - lp)
    dv = dv - .54592 * sin(lp + 3 * l)
    dv = dv - .53571 * sin(2 * d - 2 * l + 2 * f)
    dv = dv + .4784 * sin(2 * d - lp - 3 * f)
    dv = dv - .45379 * sin(2 * d + 2 * l - 2 * f)
    dv = dv - .42622 * sin(2 * d - lp - l + 2 * f)
    dv = dv + .42033 * sin(4 * f)
    dv = dv + .4134 * sin(lp + 2 * f)
    dv = dv + .40423 * sin(3 * d)
    dv = dv + .39451 * sin(6 * d - l)
    dv = dv - .38213 * sin(2 * d - lp + 2 * f)
    dv = dv - .37451 * sin(2 * d - lp + l - 2 * f)
    dv = dv - .35758 * sin(4 * d + lp - 2 * l)
    dv = dv + .34965 * sin(d + lp - 2 * l)
    dv = dv + .33979 * sin(2 * d - 3 * lp)
    dv = dv - .32866 * sin(3 * l + 2 * f)
    dv = dv + .30872 * sin(4 * d - 2 * lp - l)
    dv = dv + .30155 * sin(lp - l - 2 * f)
    dv = dv + .30086 * sin(4 * d - l - 2 * f)
    dv = dv + .2942 * sin(2 * d - 2 * lp - 2 * l)
    dv = dv + .29255 * sin(6 * d - 3 * l)
    dv = dv - .29022 * sin(2 * d + lp + 2 * l)

    # a(p,2,v) series

    dv = dv + .00487 * t2 * sin(lp)
    dv = dv - .0015 * t2 * sin(2 * d - lp - l + pi)
    dv = dv - .0012 * t2 * sin(2 * d - lp + pi)
    dv = dv + .00108 * t2 * sin(lp - l)
    dv = dv + .0008 * t2 * sin(lp + l)

    # a(p,0,v) series

    dv = dv + 14.24883 * sin(18 * ve - 16 * t - l + dtr * 26.54261)
    dv = dv + 7.06304 * sin(ll - f + dtr * .00094)
    dv = dv + 1.14307 * sin(2 * t - 2 * ju + 2 * d - l + dtr * 180.11977)
    dv = dv + .901140 * sin(4 * t - 8 * ma + 3 * ju + dtr * 285.98707)
    dv = dv + .82155 * sin(ve - t + dtr * 180.00988)
    dv = dv + .78811 * sin(18 * ve - 16 * t - 2 * l + dtr * 26.54324)
    dv = dv + .7393 * sin(18 * ve - 16 * t + dtr * 26.5456)
    dv = dv + .64371 * sin(3 * ve - 3 * t + 2 * d - l + dtr * 179.98144)
    dv = dv + .6388 * sin(t - ju + dtr * 1.2289)
    dv = dv + .56341 * sin(10 * ve - 3 * t - l + dtr * 333.30551)
    dv = dv + .49331 * sin(ll + l - f + .00127 * dtr)
    dv = dv + .49141 * sin(ll - l - f + .00127 * dtr)
    dv = dv + .44532 * sin(2 * t - 3 * ju + 2 * d - l + 10.07001 * dtr)
    dv = dv + .36061 * sin(ll + f + .00071 * dtr)
    dv = dv + .34355 * sin(2 * ve - 3 * t + 269.95393 * dtr)
    dv = dv + .32455 * sin(t - 2 * ma + 318.13776 * dtr)
    dv = dv + .30155 * sin(2 * ve - 2 * t + .20448 * dtr)
    dv = dv + .28938 * sin(t + d - f + 95.13523 * dtr)
    dv = dv + .28281 * sin(2 * t - 3 * ju + 2 * d - 2 * l + 10.03835 * dtr)
    dv = dv + .24515 * sin(2 * t - 2 * ju + 2 * d - 2 * l + .08642 * dtr)

    # a(p,1,v) series

    dv = dv + 1.6768 * t1 * sin(lp)
    dv = dv + .51642 * t1 * sin(2 * d - lp - l + pi)
    dv = dv + .41383 * t1 * sin(2 * d - lp + pi)
    dv = dv + .37115 * t1 * sin(lp - l)
    dv = dv + .2756 * t1 * sin(lp + l)
    dv = dv + .25425 * t1 * sin(18 * ve - 16 * t - l + 114.5655 * dtr)
    dv = dv + 7.1178e-02 * t1 * sin(2 * d + lp - l)
    dv = dv + .06128 * t1 * sin(2 * d + lp)
    dv = dv + .04516 * t1 * sin(d + lp + pi)
    dv = dv + .04048 * t1 * sin(2 * d - 2 * lp + pi)
    dv = dv + .03747 * t1 * sin(2 * lp)
    dv = dv + .03707 * t1 * sin(2 * d - 2 * lp - l + pi)
    dv = dv + .03649 * t1 * sin(2 * d - lp + l + pi)
    dv = dv + .02438 * t1 * sin(lp - 2 * l)
    dv = dv + .02165 * t1 * sin(2 * d - lp - 2 * l + pi)
    dv = dv + .01923 * t1 * sin(lp + 2 * l)

    plon = modulo(ll + atr * dv + dpsi)

    # geocentric ecliptic latitude (arc seconds)

    # a(c,0,u) series

    pl = 18461.23868 * sin(f)
    pl = pl + 1010.16707 * sin(l + f)
    pl = pl + 999.69358 * sin(l - f)
    pl = pl + 623.65243 * sin(2 * d - f)
    pl = pl + 199.48374 * sin(2 * d - l + f)
    pl = pl + 166.5741 * sin(2 * d - l - f)
    pl = pl + 117.26069 * sin(2 * d + f)
    pl = pl + 61.91195 * sin(2 * l + f)
    pl = pl + 33.3572 * sin(2 * d + l - f)
    pl = pl + 31.75967 * sin(2 * l - f)
    pl = pl + 29.57658 * sin(2 * d - lp - f)
    pl = pl + 15.56626 * sin(2 * d - 2 * l - f)
    pl = pl + 15.12155 * sin(2 * d + l + f)
    pl = pl - 12.09414 * sin(2 * d + lp - f)
    pl = pl + 8.86814 * sin(2 * d - lp - l + f)
    pl = pl + 7.95855 * sin(2 * d - lp + f)
    pl = pl + 7.43455 * sin(2 * d - lp - l - f)
    pl = pl - 6.73143 * sin(lp - l - f)
    pl = pl + 6.57957 * sin(4 * d - l - f)
    pl = pl - 6.46007 * sin(lp + f)
    pl = pl - 6.29648 * sin(3 * f)
    pl = pl - 5.63235 * sin(lp - l + f)
    pl = pl - 5.3684 * sin(d + f)
    pl = pl - 5.31127 * sin(lp + l + f)
    pl = pl - 5.07591 * sin(lp + l - f)
    pl = pl - 4.83961 * sin(lp - f)
    pl = pl - 4.80574 * sin(d - f)
    pl = pl + 3.98405 * sin(3 * l + f)
    pl = pl + 3.67446 + sin(4 * d - f)
    pl = pl + 2.99848 * sin(4 * d - l + f)
    pl = pl + 2.79864 * sin(l - 3 * f)
    pl = pl + 2.41388 * sin(4 * d - 2 * l + f)
    pl = pl + 2.18631 * sin(2 * d - 3 * f)
    pl = pl + 2.14617 * sin(2 * d + 2 * l - f)
    pl = pl + 1.76598 * sin(2 * d - lp + l - f)
    pl = pl - 1.62442 * sin(2 * d - 2 * l + f)
    pl = pl + 1.5813 * sin(3 * l - f)
    pl = pl + 1.51975 * sin(2 * d + 2 * l + f)
    pl = pl - 1.51563 * sin(2 * d - 3 * l - f)
    pl = pl - 1.31782 * sin(2 * d + lp - l + f)
    pl = pl - 1.26427 * sin(2 * d + lp + f)
    pl = pl + 1.19187 * sin(4 * d + f)
    pl = pl + 1.13461 * sin(2 * d - lp + l + f)
    pl = pl + 1.08578 * sin(2 * d - 2 * lp - f)
    pl = pl - 1.01938 * sin(l + 3 * f)
    pl = pl - .822710 * sin(2 * d + lp + l - f)
    pl = pl + .80422 * sin(d + lp - f)
    pl = pl + .80259 * sin(d + lp + f)
    pl = pl - .79319 * sin(lp - 2 * l - f)
    pl = pl - .79101 * sin(2 * d + lp - l - f)
    pl = pl - .66741 * sin(d + l + f)
    pl = pl + .65022 * sin(2 * d - lp - 2 * l - f)
    pl = pl - .63881 * sin(lp + 2 * l + f)
    pl = pl + .63371 * sin(4 * d - 2 * l - f)
    pl = pl + .59577 * sin(4 * d - lp - l - f)
    pl = pl - .58893 * sin(d + l - f)
    pl = pl + .47338 * sin(4 * d + l - f)
    pl = pl - .42989 * sin(d - l - f)
    pl = pl + .41494 * sin(4 * d - lp - f)
    pl = pl + .3835 * sin(2 * d - 2 * lp + f)
    pl = pl - .35183 * sin(3 * d - f)
    pl = pl + .33881 * sin(4 * d - lp - l + f)
    pl = pl + .32906 * sin(2 * d - l - 3 * f)

    # a(p,0,u) series

    pl = pl + 8.04508 * sin(ll + 180.00071 * dtr)
    pl = pl + 1.51021 * sin(t + d + 276.68007 * dtr)
    pl = pl + .63037 * sin(18 * ve - 16 * t - l + f + 26.54287 * dtr)
    pl = pl + .63014 * sin(18 * ve - 16 * t - l - f + 26.54272 * dtr)
    pl = pl + .45586 * sin(ll - l + .00075 * dtr)
    pl = pl + .41571 * sin(ll + l + 180.00069 * dtr)
    pl = pl + .32622 * sin(ll - 2 * f + .00086 * dtr)
    pl = pl + .29854 * sin(ll - 2 * d + .00072 * dtr)

    # a(p,1,u) series

    pl = pl + .0743 * t1 * sin(2 * d - lp - f + pi)
    pl = pl + .03043 * t1 * sin(2 * d + lp - f)
    pl = pl + .02229 * t1 * sin(2 * d - lp - l + f + pi)
    pl = pl + .01999 * t1 * sin(2 * d - lp + f + pi)
    pl = pl + .01869 * t1 * sin(2 * d - lp - l - f + pi)
    pl = pl + .01696 * t1 * sin(lp - l - f)
    pl = pl + .01623 * t1 * sin(lp + f)

    plat = atr * pl

    # geocentric right ascension and declination

    a = sin(plon) * cos(obliq) - tan(plat) * sin(obliq)

    b = cos(plon)

    rasc = atan3(a, b)

    decl = asin(sin(plat) * cos(obliq) + cos(plat) * sin(obliq) * sin(plon))

    # geocentric position vector of the moon (kilometers)

    return (rmm * cos(rasc) * cos(decl),
            rmm * sin(rasc) * cos(decl),
            rmm * sin(decl))


def gsite(angle):
    # ground site position vector (kilometers)

    slat = sin(obslat)
    clat = cos(obslat)

    sangle = sin(angle)
    cangle = cos(angle)

    b = sqrt(1.0 - (2.0 * flat - flat * flat) * slat * slat)

    c = reqm / b + 0.001 * obsalt

    d = reqm * (1.0 - flat) * (1.0 - flat) / b + 0.001 * obsalt

    return (c * clat * cangle, c * clat * sangle, d * slat)


def eci2topo(gast, robj):
    # eci position vector to topocentric coordinates
    # returns (azimuth, elevation, x, y, z)

    obslst = modulo(gast + obslong)

    rsite = gsite(obslst)

    rhoijk = (robj[0] - rsite[0], robj[1] - rsite[1], robj[2] - rsite[2])

    rhohatijk = uvector(rhoijk)

    sobslat = sin(obslat)
    cobslat = cos(obslat)

    sobslst = sin(obslst)
    cobslst = cos(obslst)

    # eci-to-sez transformation (matrix * vector)

    s1 = sobslat * cobslst * rhohatijk[0] + sobslat * sobslst * rhohatijk[1] + (-cobslat) * rhohatijk[2]
    s2 = (-sobslst) * rhohatijk[0] + cobslst * rhohatijk[1] + 0.0 * rhohatijk[2]
    s3 = cobslat * cobslst * rhohatijk[0] + cobslat * sobslst * rhohatijk[1] + sobslat * rhohatijk[2]

    elevation = asin(s3)

    azimuth = atan3(s2, -s1)

    return (azimuth, elevation, rhoijk[0], rhoijk[1], rhoijk[2])


def findleap(jday):
    # number of leap seconds for utc julian day

    if jday <= LEAP[0][0]:
        return LEAP[0][1]

    if jday >= LEAP[27][0]:
        return LEAP[27][1]

    for i in range(27):
        if jday >= LEAP[i][0] and jday < LEAP[i + 1][0]:
            return LEAP[i][1]

    return LEAP[27][1]


def utc2tdb(jdutc):
    # convert UTC julian date to TDB julian date

    leapsecond = findleap(jdutc)

    corr = (leapsecond + 32.184) / 86400.0

    jdtt = jdutc + corr

    t = (jdtt - 2451545.0) / 36525.0

    # correction in microseconds

    corr = 1656.675 * sin(dtr * (35999.3729 * t + 357.5287))
    corr = corr + 22.418 * sin(dtr * (32964.467 * t + 246.199))
    corr = corr + 13.84 * sin(dtr * (71998.746 * t + 355.057))
    corr = corr + 4.77 * sin(dtr * (3034.906 * t + 25.463))
    corr = corr + 4.677 * sin(dtr * (34777.259 * t + 230.394))
    corr = corr + 10.216 * t * sin(dtr * (35999.373 * t + 243.451))
    corr = corr + 0.171 * t * sin(dtr * (71998.746 * t + 240.98))
    corr = corr + 0.027 * t * sin(dtr * (1222.114 * t + 194.661))
    corr = corr + 0.027 * t * sin(dtr * (3034.906 * t + 336.061))
    corr = corr + 0.026 * t * sin(dtr * (-20.186 * t + 9.382))
    corr = corr + 0.007 * t * sin(dtr * (29929.562 * t + 264.911))
    corr = corr + 0.006 * t * sin(dtr * (150.678 * t + 59.775))
    corr = corr + 0.005 * t * sin(dtr * (9037.513 * t + 256.025))
    corr = corr + 0.043 * t * sin(dtr * (35999.373 * t + 151.121))

    corr = 0.000001 * corr / 86400.0

    return jdtt + corr


def jdfunc(jdutc):
    # objective function for tdb2utc
    return utc2tdb(jdutc) - jdsaved


def tdb2utc(jdtdb):
    # convert TDB julian day to UTC julian day (Brent's method)

    global jdsaved

    jdsaved = jdtdb

    x1 = jdsaved - 0.1
    x2 = jdsaved + 0.1

    xroot, froot = brent(jdfunc, x1, x2, 1.0e-8)

    return xroot


def sefunc(x):
    # solar eclipse objective function

    global elev_minima

    jdtdb = jdtdbi + x

    rmoon_ = moon(jdtdb)

    rsun_ = sun(jdtdb)

    rm2s = (rsun_[0] - rmoon_[0], rsun_[1] - rmoon_[1], rsun_[2] - rmoon_[2])

    # topocentric coordinates of the moon

    jdutc = tdb2utc(jdtdb)

    gast = gast2(jdutc)

    dvec = eci2topo(gast, rmoon_)

    rtmoon = (dvec[2], dvec[3], dvec[4])

    rtmmoon = vecmag(rtmoon)

    # topocentric elevation of the moon (radians)

    elev_minima = dvec[1]

    # shadow axis unit position vector

    rm2smag = vecmag(rm2s)

    uaxis = (-rm2s[0] / rm2smag, -rm2s[1] / rm2smag, -rm2s[2] / rm2smag)

    # moon-to-observer unit position vector

    um2o = (-rtmoon[0] / rtmmoon, -rtmoon[1] / rtmmoon, -rtmoon[2] / rtmmoon)

    # penumbra shadow angle

    pangle = asin(radmoon / rtmmoon) + asin((radsun + radmoon) / rm2smag)

    # separation angle between anti-sun and moon-to-observer vectors

    cpsi = uaxis[0] * um2o[0] + uaxis[1] * um2o[1] + uaxis[2] * um2o[2]

    psi = acos(cpsi)

    return psi - pangle


def brent(func, x1, x2, tol):
    # real root of a single non-linear function (Brent's method)
    # returns (xroot, froot)

    eps = 2.23e-16

    e = 0.0
    d = 0.0
    c = 0.0

    a = x1
    b = x2

    fa = func(a)
    fb = func(b)

    fcc = fb

    for _ in range(50):

        if fb * fcc > 0.0:
            c = a
            fcc = fa
            d = b - a
            e = d

        if abs(fcc) < abs(fb):
            a = b
            b = c
            c = a
            fa = fb
            fb = fcc
            fcc = fa

        tol1 = 2.0 * eps * abs(b) + 0.5 * tol

        xm = 0.5 * (c - b)

        if abs(xm) <= tol1 or fb == 0.0:
            break

        if abs(e) >= tol1 and abs(fa) > abs(fb):

            s = fb / fa

            if a == c:
                p = 2.0 * xm * s
                q = 1.0 - s
            else:
                q = fa / fcc
                r = fb / fcc
                p = s * (2.0 * xm * q * (q - r) - (b - a) * (r - 1.0))
                q = (q - 1.0) * (r - 1.0) * (s - 1.0)

            if p > 0.0:
                q = -q

            p = abs(p)

            xmin = abs(e * q)

            tmp = 3.0 * xm * q - abs(tol1 * q)

            if xmin < tmp:
                xmin = tmp

            if 2.0 * p < xmin:
                e = d
                d = p / q
            else:
                d = xm
                e = d

        else:

            d = xm
            e = d

        a = b

        fa = fb

        if abs(d) > tol1:
            b = b + d
        else:
            b = b + sgn(xm) * tol1

        fb = func(b)

    return b, fb


def broot(x1in, x2in, factor, dxmax):
    # bracket a single root; returns (x1out, x2out)

    f1 = sefunc(x1in)

    x3 = x1in

    dx = x2in - x1in

    while True:

        # geometrically accelerate the second point

        x2in = x2in + factor * (x2in - x3)

        f2 = sefunc(x2in)

        # rectification

        if abs(x2in - x3) > dxmax:
            x3 = x2in - dx

        if f1 * f2 < 0.0:
            break

    return x1in, x2in


def minima(a, b, tolm):
    # one-dimensional minimization (Brent's method) of sefunc
    # returns (xmin, fmin)

    epsm = 2.23e-16

    c = 0.38196601125

    d = 0.0
    e = 0.0
    p = 0.0
    q = 0.0
    r = 0.0

    x = a + c * (b - a)

    w = x
    v = w

    fx = sefunc(x)

    fw = fx
    fv = fw

    for it in range(1, 101):

        if it > 50:
            print("error in function minima!")
            print("(more than 50 iterations)")
            raise SystemExit

        xm = 0.5 * (a + b)

        tol1 = tolm * abs(x) + epsm

        t2 = 2.0 * tol1

        if abs(x - xm) <= (t2 - 0.5 * (b - a)):
            return x, fx

        if abs(e) > tol1:

            r = (x - w) * (fx - fv)

            q = (x - v) * (fx - fw)

            p = (x - v) * q - (x - w) * r

            q = 2.0 * (q - r)

            if q > 0.0:
                p = -p

            q = abs(q)

            r = e

            e = d

        if (abs(p) >= abs(0.5 * q * r)) or (p <= q * (a - x)) or (p >= q * (b - x)):

            if x >= xm:
                e = a - x
            else:
                e = b - x

            d = c * e

        else:

            d = p / q

            u = x + d

            if ((u - a) < t2) or ((b - u) < t2):
                d = sgn(xm - x) * tol1

        if abs(d) >= tol1:
            u = x + d
        else:
            u = x + sgn(d) * tol1

        fu = sefunc(u)

        if fu <= fx:

            if u >= x:
                a = x
            else:
                b = x

            v = w
            fv = fw
            w = x
            fw = fx
            x = u
            fx = fu

        else:

            if u < x:
                a = u
            else:
                b = u

            if (fu <= fw) or (w == x):
                v = w
                fv = fw
                w = u
                fw = fu
            elif (fu <= fv) or (v == x) or (v == w):
                v = u
                fv = fu

    return x, fx


def julian(month, day, year):
    # Gregorian date to julian day

    y = year
    m = month

    b = 0.0
    c = 0.0

    if m <= 2.0:
        y = y - 1.0
        m = m + 12.0

    if y < 0.0:
        c = -0.75

    if year < 1582.0:
        pass
    elif year > 1582.0:
        a = trunc(y / 100.0)
        b = 2.0 - a + trunc(a / 4.0)
    elif month < 10.0:
        pass
    elif month > 10.0:
        a = trunc(y / 100.0)
        b = 2.0 - a + trunc(a / 4.0)
    elif day <= 4.0:
        pass
    elif day > 14.0:
        a = trunc(y / 100.0)
        b = 2.0 - a + trunc(a / 4.0)
    else:
        print("this date does not exist!!")
        raise SystemExit

    return trunc(365.25 * y + c) + trunc(30.6001 * (m + 1.0)) + day + b + 1720994.5


def gdate(jday):
    # Julian day to Gregorian date; returns (month, day, year)

    z = trunc(jday + 0.5)

    f = jday + 0.5 - z

    if z < 2299161:
        a = z
    else:
        alpha = trunc((z - 1867216.25) / 36524.25)
        a = z + 1.0 + alpha - trunc(alpha / 4.0)

    b = a + 1524.0

    c = trunc((b - 122.1) / 365.25)

    d = trunc(365.25 * c)

    e = trunc((b - d) / 30.6001)

    day = b - d - trunc(30.6001 * e) + f

    if e < 13.5:
        month = e - 1.0
    else:
        month = e - 13.0

    if month > 2.5:
        year = c - 4716.0
    else:
        year = c - 4715.0

    return month, day, year


def jd2str(jdutc):
    # print julian day as calendar date and UTC time

    cmonth, day, year = gdate(jdutc)

    print("calendar date  ", MONTHS[int(cmonth) - 1], " ", str(int(floor(day))), " ", str(int(year)))

    print(" ")

    thr0 = 24.0 * (day - floor(day))

    thr = floor(thr0)

    tmin0 = 60.0 * (thr0 - thr)

    tmin = floor(tmin0)

    tsec = 60.0 * (tmin0 - tmin)

    print("UTC time       ", "%d hours %d minutes %.2f seconds" % (int(thr), int(tmin), tsec))


def deg2str(dd):
    # decimal degrees to degrees, minutes, seconds string

    d1 = abs(dd)

    d = trunc(d1)

    d1 = (d1 - d) * 60.0

    m = trunc(d1)

    s = (d1 - m) * 60.0

    if dd < 0.0:
        if d != 0:
            d = -d
        elif m != 0:
            m = -m
        else:
            s = -s

    return "%d deg %d min %.2f sec" % (d, m, s)


def eclprint(iflag, jdtdb):
    # print solar eclipse conditions

    global jdprint

    if iflag == 1:
        print(" ")
        print(" ")
        print("begin penumbral phase of solar eclipse")
        print(" ")
        jdprint = jdtdb

    if iflag == 2:
        print(" ")
        print(" ")
        print("greatest eclipse conditions")
        print(" ")

    if iflag == 3:
        print(" ")
        print(" ")
        print("end penumbral phase of solar eclipse")
        print(" ")

    # UTC julian date

    jdutc = tdb2utc(jdtdb)

    jd2str(jdutc)

    print(" ")

    print("UTC julian day     ", "%.8f" % jdutc)

    # topocentric coordinates of the moon

    gast = gast2(jdutc)

    rmoon_ = moon(jdtdb)

    dvec = eci2topo(gast, rmoon_)

    print(" ")

    print("topocentric coordinates")

    print(" ")

    print("lunar azimuth angle    ", deg2str(rtd * dvec[0]))

    print(" ")

    print("lunar elevation angle  ", deg2str(rtd * dvec[1]))

    # topocentric coordinates of the sun

    rsun_ = sun(jdtdb)

    dvec = eci2topo(gast, rsun_)

    print(" ")

    print("solar azimuth angle    ", deg2str(rtd * dvec[0]))

    print(" ")

    print("solar elevation angle  ", deg2str(rtd * dvec[1]))

    # event duration

    if iflag == 3:

        deltat = 24.0 * (jdtdb - jdprint)

        print(" ")

        print("event duration   ", "%.8f hours" % deltat)

        print(" ")


def events1(ti, tf, topt):
    # compute and display eclipse events

    global trr

    factor = 0.25            # geometric acceleration factor

    dxmax = 600.0 / 86400.0  # rectification interval

    rtol = 1.0e-8            # root-finding convergence tolerance

    # event start conditions

    t1in = topt

    t2in = t1in - 30.0 / 86400.0

    t1out, t2out = broot(t1in, t2in, factor, dxmax)

    troot, froot = brent(sefunc, t1out, t2out, rtol)

    if troot < ti:
        troot = ti
        froot = sefunc(ti)

    jdate = jdtdbi + troot

    eclprint(1, jdate)

    # greatest eclipse conditions

    jdate = jdtdbi + topt

    eclprint(2, jdate)

    # event end conditions

    t2in = t1in + 30.0 / 86400.0

    t1out, t2out = broot(t1in, t2in, factor, dxmax)

    troot, froot = brent(sefunc, t1out, t2out, rtol)

    if troot > tf:
        troot = tf
        froot = sefunc(tf)

    jdate = jdtdbi + troot

    eclprint(3, jdate)

    # save current value of root

    trr = troot


def ecl_event(ti, tf, dt, dtsml):
    # predict solar eclipse events

    tolm = 1.0e-6

    iend = 0

    # check the initial time for a minimum

    fmin1 = sefunc(ti)

    tmin1 = ti

    ftemp = sefunc(ti + dtsml)

    df = ftemp - fmin1

    dflft = df

    el = ti

    er = el

    t = ti

    # if the slope is positive and the minimum is
    # negative, calculate event conditions at the initial time

    if df > 0.0 and fmin1 < 0.0:
        events1(ti, tf, tmin1)

    for _iter1 in range(100000):

        # find where function first starts decreasing

        for _iter2 in range(100000):

            if df <= 0.0:
                break

            t = t + dt

            if t >= tf:

                # check final time for a minimum

                if iend == 1:
                    return

                fmin1 = sefunc(tf)

                ftemp = sefunc(tf - dtsml)

                df = fmin1 - ftemp

                tmin1 = tf

                if df < 0.0:

                    if fmin1 < 0.0:
                        events1(ti, tf, tmin1)

                    return

                if dflft > 0.0:
                    break

                er = tf

                iend = 1

            ft = sefunc(t)

            ftemp = sefunc(t - dtsml)

            df = ft - ftemp

        # function decreasing - find where function
        # first starts increasing

        for _iter3 in range(100000):

            el = t

            dflft = df

            t = t + dt

            if t >= tf:

                # check final time for a minimum

                if iend == 1:
                    return

                fmin1 = sefunc(tf)

                ftemp = sefunc(tf - dtsml)

                df = fmin1 - ftemp

                tmin1 = tf

                if df < 0.0:

                    if fmin1 < 0.0:
                        events1(ti, tf, tmin1)

                    return

                if dflft > 0.0:
                    return

                er = tf

                iend = 1

            ft = sefunc(t)

            ftemp = sefunc(t - dtsml)

            df = ft - ftemp

            if df > 0.0:
                break

        er = t

        # calculate minimum using Brent's method

        tmin1, fmin1 = minima(el, er, tolm)

        el = er

        dflft = df

        # if the minimum is negative and the topocentric
        # elevation angle of the moon is positive,
        # calculate event conditions for this minimum

        if fmin1 < 0.0 and elev_minima > 0.0:

            events1(ti, tf, tmin1)

            t = trr


def getdate():
    # interactive request calendar date

    while True:
        print(" ")
        print("please input the initial calendar date")
        print(" ")
        print("(month [1 - 12], day [1 - 31], year [yyyy])")
        print("< for example, october 21, 1986 is input as 10,21,1986 >")
        print("< b.c. dates are negative, a.d. dates are positive >")
        print("< the day of the month may also include a decimal part >")
        print(" ")

        parts = input().split(",")

        try:
            month = float(parts[0])
            day = float(parts[1])
            year = float(parts[2])
        except (ValueError, IndexError):
            continue

        if 1 <= month <= 12 and 1 <= day <= 31:
            return month, day, year


def observer():
    # interactive request of latitude, longitude and altitude
    # returns (obslat, obslong, obsalt)

    while True:
        print("please input the geographic latitude of the observer")
        print("(degrees [-90 to +90], minutes [0 - 60], seconds [0 - 60])")
        print("(north latitudes are positive, south latitudes are negative)")

        parts = input().split(",")

        try:
            latdeg = parts[0].strip()
            latmin = float(parts[1])
            latsec = float(parts[2])
        except (ValueError, IndexError):
            continue

        if abs(float(latdeg)) <= 90.0 and 0.0 <= latmin <= 60.0 and 0.0 <= latsec <= 60.0:
            break

    if latdeg[0:2] == "-0":
        lat = -dtr * (latmin / 60.0 + latsec / 3600.0)
    elif float(latdeg) == 0.0:
        lat = dtr * (latmin / 60.0 + latsec / 3600.0)
    else:
        v = float(latdeg)
        lat = dtr * (sgn(v) * (abs(v) + latmin / 60.0 + latsec / 3600.0))

    while True:
        print("")
        print("please input the geographic longitude of the observer")
        print("(degrees [0 - 360], minutes [0 - 60], seconds [0 - 60])")
        print("(east longitude is positive, west longitude is negative)")

        parts = input().split(",")

        try:
            londeg = parts[0].strip()
            lonmin = float(parts[1])
            lonsec = float(parts[2])
        except (ValueError, IndexError):
            continue

        if abs(float(londeg)) <= 360.0 and 0.0 <= lonmin <= 60.0 and 0.0 <= lonsec <= 60.0:
            break

    if londeg[0:2] == "-0":
        lon = -dtr * (lonmin / 60.0 + lonsec / 3600.0)
    elif float(londeg) == 0.0:
        lon = dtr * (lonmin / 60.0 + lonsec / 3600.0)
    else:
        v = float(londeg)
        lon = dtr * (sgn(v) * (abs(v) + lonmin / 60.0 + lonsec / 3600.0))

    print(" ")

    print("please input the altitude of the observer (meters)")
    print("(positive above sea level, negative below sea level)")

    alt = float(input())

    return lat, lon, alt


def main():
    global jdtdbi, obslat, obslong, obsalt

    print(" ")
    print("program solar_eclipse")
    print("=====================")

    # initial calendar date

    cmonth, cday, cyear = getdate()

    print(" ")

    obslat, obslong, obsalt = observer()

    # initial utc julian day

    jdutc = julian(cmonth, cday, cyear)

    # initial tdb julian date

    jdtdbi = utc2tdb(jdutc)

    print(" ")

    print("please input the search duration in days")

    ndays = float(input())

    tstart = ticks_ms()

    print(" ")
    print("searching for solar eclipse ...")
    print(" ")

    # search parameters

    ti = 0.0
    tf = ndays
    dt = 0.25
    dtsml = 0.1

    ecl_event(ti, tf, dt, dtsml)

    print("Time taken : ", ticks_diff(ticks_ms(), tstart) / 1000.0, " Seconds")


main()
