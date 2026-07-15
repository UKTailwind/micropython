# Simple plotting for the Pico Computer 3 -- draw data or a function on the HDMI
# screen. Aimed at maths/science education: one call, autoscaled, with axes and
# range labels.
#
#     import pcplot, math
#     pcplot.plot([1, 4, 9, 16, 25])                 # a list
#     pcplot.plot(math.sin, (0, 2 * math.pi))        # a function over a range
#     pcplot.plot([series_a, series_b], style="line")  # several series
#     pcplot.plot(samples, style="bar")
#
# `plot` is injected into the REPL, so you can just call plot(...).

import hdmi

# Default series colours (RGB888), cycled for multiple series.
_COLOURS = (0x00FFFF, 0xFFFF00, 0x00FF00, 0xFF4080, 0xFF8000, 0xFFFFFF)
_AXIS = 0x808080
_LABEL = 0xD2D2D2


def _is_seq(v):
    # True for a list/tuple/ndarray, False for a number or string. (MicroPython
    # doesn't expose __len__ via hasattr, so probe len() directly.)
    if isinstance(v, str):
        return False
    try:
        len(v)
        return True
    except TypeError:
        return False


def _series(y, x):
    """Normalise the arguments into a list of (xs, ys) pairs (plain lists)."""
    if callable(y):                                  # a function over x=(a,b[,n])
        a, b = x[0], x[1]
        n = x[2] if len(x) > 2 else 200
        xs = [a + (b - a) * i / (n - 1) for i in range(n)] if n > 1 else [a]
        return [(xs, [y(t) for t in xs])]
    if len(y) and _is_seq(y[0]):                     # several series
        out = []
        for s in y:
            xs = list(x) if x is not None else list(range(len(s)))
            out.append((xs, [float(v) for v in s]))
        return out
    xs = list(x) if x is not None else list(range(len(y)))
    return [(xs, [float(v) for v in y])]


def _bounds(series):
    xlo = ylo = 1e30
    xhi = yhi = -1e30
    for xs, ys in series:
        for v in xs:
            if v < xlo:
                xlo = v
            if v > xhi:
                xhi = v
        for v in ys:
            if v < ylo:
                ylo = v
            if v > yhi:
                yhi = v
    if xhi <= xlo:
        xhi = xlo + 1
    if yhi <= ylo:                                   # flat data -> pad so it shows
        yhi = ylo + 1
        ylo -= 1
    return xlo, xhi, ylo, yhi


def _fmt(v):
    a = abs(v)
    if a != 0 and (a >= 100000 or a < 0.001):
        return "%.1e" % v
    if a >= 100:
        return "%.0f" % v
    return "%.3g" % v


def plot(y, x=None, style="line", colour=None, box=None, clear=True,
         axes=True, d=None):
    """Plot `y` on the HDMI screen, autoscaled with axes and range labels.

    y      : a sequence, a list of sequences (several series), or a function
    x      : x values (a sequence); for a function, the range (a, b[, n])
    style  : "line" | "scatter" | "bar"
    colour : one RGB888 colour, or a list per series (default: a palette)
    box    : (x, y, w, h) plot area in pixels (default: full screen with margins)
    clear  : blank the screen first (True) or draw over what's there (False)
    """
    d = d or hdmi.fb()
    W, H = hdmi.width(), hdmi.height()
    if box is None:
        box = (34, 6, W - 40, H - 24)                # room for labels L and below
    bx, by, bw, bh = box
    series = _series(y, x)
    xlo, xhi, ylo, yhi = _bounds(series)
    xr = xhi - xlo
    yr = yhi - ylo

    def px(vx):
        return bx + int((vx - xlo) / xr * (bw - 1))

    def py(vy):
        return by + (bh - 1) - int((vy - ylo) / yr * (bh - 1))

    if clear:
        d.fill(0)
    if axes:
        c = d.colour(_AXIS)
        d.rect(bx, by, bw, bh, c)
        if ylo < 0 < yhi:                            # x-axis (y = 0)
            d.hline(bx, py(0), bw, c)
        if xlo < 0 < xhi:                            # y-axis (x = 0)
            d.vline(px(0), by, bh, c)
        lc = d.colour(_LABEL)
        d.text(_fmt(yhi), 0, by, lc, font=7)
        d.text(_fmt(ylo), 0, by + bh - 8, lc, font=7)
        d.text(_fmt(xlo), bx, by + bh + 3, lc, font=7)
        d.text(_fmt(xhi), bx + bw - len(_fmt(xhi)) * 6, by + bh + 3, lc, font=7)

    base = py(0) if ylo < 0 < yhi else by + bh - 1    # bar baseline
    for si, (xs, ys) in enumerate(series):
        if colour is None:
            col = d.colour(_COLOURS[si % len(_COLOURS)])
        elif _is_seq(colour):
            col = d.colour(colour[si % len(colour)])
        else:
            col = d.colour(colour)
        if style == "bar":
            n = len(ys)
            bw1 = max(1, (bw // n) - 1)
            for i in range(n):
                yb = py(ys[i])
                top = min(yb, base)
                d.fill_rect(px(xs[i]) - bw1 // 2, top, bw1, abs(base - yb) + 1, col)
        elif style == "scatter":
            for i in range(len(ys)):
                cx, cy = px(xs[i]), py(ys[i])
                d.fill_rect(cx - 1, cy - 1, 3, 3, col)
        else:                                         # line
            lx, ly = px(xs[0]), py(ys[0])
            for i in range(1, len(ys)):
                nx, ny = px(xs[i]), py(ys[i])
                d.line(lx, ly, nx, ny, col)
                lx, ly = nx, ny
