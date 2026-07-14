# On-screen GUI controls for the Pico Computer 3 -- MMBasic's GUI set
# (Micromite Plus), reimagined as a small Python widget toolkit.
#
# MMBasic uses reference numbers and a global control list; this is the same set
# of controls with the same behaviour, but object-oriented and a little cleaner
# visually (rounded controls, the bitmap fonts from hdmi.text). A GUI manager
# owns the controls and dispatches pointer + keyboard events:
#
#     import pcgui
#     from pcgfx import GREEN, RED, YELLOW, WHITE
#
#     g = pcgui.GUI()                       # draws on hdmi.fb()
#     g.start()                             # begin capturing the keyboard
#     g.frame(10, 10, 150, 90, "Pump")
#     led = g.led(30, 40, 8, "Run", GREEN)
#     def toggle(sw):
#         led.value = sw.value
#     g.switch(30, 60, 70, 26, "ON|OFF", callback=toggle)
#     while True:
#         g.poll()                          # dispatch touches/clicks + typing
#
# Pointer input comes from a USB mouse (mouse("X"/"Y"/"L")) or a USB touch panel
# (touch("X"/"Y"/"DOWN")); text entry from the USB keyboard (keyboard.on_key).

import time

import hdmi
import keyboard

import pcgfx

# --- style ------------------------------------------------------------------
# Colours are 24-bit RGB (the pcgfx palette constants), resolved to the display
# format at draw time. These are the defaults; every control can override them.
FG = pcgfx.WHITE
BG = 0x202020          # control face
FRAME = pcgfx.LITEGRAY
ACCENT = 0x2864FF      # selection / focus / slider fill
TRACK = 0x404040       # gauge / slider / bar track
RADIUS = 5             # default corner radius


def _scale(c, f):
    """Scale an RGB888 colour's brightness by factor f (0..~2, clamped)."""
    r = min(255, int(((c >> 16) & 0xFF) * f))
    g = min(255, int(((c >> 8) & 0xFF) * f))
    b = min(255, int((c & 0xFF) * f))
    return (r << 16) | (g << 8) | b


def _dim(c):
    return _scale(c, 0.45)


# --- base control -----------------------------------------------------------
class Control:
    editable = False   # textboxes set this (they take keyboard focus)
    interactive = True  # False for indicators (LED / gauge / caption / frame)

    def __init__(self, g, x, y, w, h, fg, bg, font):
        self.g = g
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        self.fg = FG if fg is None else fg
        self.bg = BG if bg is None else bg
        self.font = font
        self.enabled = True
        self.hidden = False
        self.callback = None
        self._value = 0

    # value is the control's state (number, 0/1, or string); setting it redraws.
    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, v):
        self._value = v
        self.redraw()

    def redraw(self):
        if not self.hidden:
            self.draw()

    def disable(self, off=True):
        self.enabled = not off
        self.redraw()

    def hide(self, on=True):
        self.hidden = on
        if on:
            self.g._erase(self)
        else:
            self.draw()

    def hit(self, px, py):
        return self.x <= px < self.x + self.w and self.y <= py < self.y + self.h

    # event hooks (pointer in screen coords); overridden by interactive controls
    def press(self, px, py):
        pass

    def drag(self, px, py):
        pass

    def release(self, px, py):
        pass

    def draw(self):
        pass

    # colour helpers -> native format, dimmed when disabled
    def _c(self, rgb):
        return self.g.c(rgb if self.enabled else _dim(rgb))

    def _fire(self):
        if self.callback:
            try:
                self.callback(self)
            except Exception as e:
                import sys

                sys.print_exception(e)


# --- caption / frame (static) ----------------------------------------------
class Caption(Control):
    interactive = False

    def __init__(self, g, x, y, text, fg, bg, font, just):
        fw, fh = g.metrics(font)
        w = len(text) * fw
        super().__init__(g, x, y, w, fh, fg, bg, font)
        self.text = text
        self.just = (just or "LT").upper()

    def draw(self):
        fw, fh = self.g.metrics(self.font)
        w = len(self.text) * fw
        x, y = self.x, self.y
        if "C" in self.just:
            x -= w // 2
        elif "R" in self.just:
            x -= w
        if "M" in self.just:
            y -= fh // 2
        elif "B" in self.just:
            y -= fh
        bg = -1 if self.bg is None else self.g.c(self.bg)
        self.g.d.text(self.text, x, y, self._c(self.fg), font=self.font, bg=bg)


class Frame(Control):
    interactive = False

    def __init__(self, g, x, y, w, h, title, fg, font):
        super().__init__(g, x, y, w, h, fg if fg is not None else FRAME, BG, font)
        self.title = title

    def draw(self):
        d = self.g.d
        d.rbox(self.x, self.y, self.w, self.h, RADIUS, self._c(self.fg))
        if self.title:
            fw, fh = self.g.metrics(self.font)
            tw = len(self.title) * fw
            tx = self.x + 10
            # blank the border under the title, then draw it straddling the top
            d.fill_rect(tx - 2, self.y, tw + 4, 1, self.g.c(self.g.face))
            d.text(self.title, tx, self.y - fh // 2, self._c(self.fg),
                   font=self.font, bg=self.g.c(self.g.face))


# --- button / switch --------------------------------------------------------
class Button(Control):
    def __init__(self, g, x, y, w, h, text, fg, bg, font):
        super().__init__(g, x, y, w, h, fg, bg if bg is not None else ACCENT, font)
        self.text = text
        self._down = False

    def draw(self):
        d = self.g.d
        face = _scale(self.bg, 0.7 if self._down else 1.0)
        d.rbox(self.x, self.y, self.w, self.h, RADIUS, self._c(_scale(self.bg, 0.6)),
               fill=self._c(face))
        fw, fh = self.g.metrics(self.font)
        tx = self.x + (self.w - len(self.text) * fw) // 2
        ty = self.y + (self.h - fh) // 2 + (1 if self._down else 0)
        d.text(self.text, tx, ty, self._c(self.fg), font=self.font)

    def press(self, px, py):
        self._down = True
        self.redraw()

    def release(self, px, py):
        self._down = False
        self.redraw()
        if self.hit(px, py):
            self._value = 1
            self._fire()
            self._value = 0


class Switch(Control):
    def __init__(self, g, x, y, w, h, labels, fg, bg, font):
        super().__init__(g, x, y, w, h, fg, bg, font)
        parts = (labels or "ON|OFF").split("|")
        self.on_label = parts[0]
        self.off_label = parts[1] if len(parts) > 1 else "OFF"

    def draw(self):
        d = self.g.d
        on = bool(self._value)
        face = 0x208020 if on else _scale(self.bg, 1.0)
        d.rbox(self.x, self.y, self.w, self.h, RADIUS,
               self._c(_scale(face, 0.6)), fill=self._c(face))
        label = self.on_label if on else self.off_label
        fw, fh = self.g.metrics(self.font)
        tx = self.x + (self.w - len(label) * fw) // 2
        ty = self.y + (self.h - fh) // 2
        d.text(label, tx, ty, self._c(self.fg), font=self.font)

    def release(self, px, py):
        if self.hit(px, py):
            self._value = 0 if self._value else 1
            self.redraw()
            self._fire()


# --- checkbox / radio / led -------------------------------------------------
class CheckBox(Control):
    def __init__(self, g, x, y, size, label, fg, font):
        fw, fh = g.metrics(font)
        h = max(size, fh)
        w = size + 6 + len(label) * fw
        super().__init__(g, x, y, w, h, fg, BG, font)
        self.size = size
        self.label = label

    def draw(self):
        d = self.g.d
        s = self.size
        col = self._c(self.fg)
        d.fill_rect(self.x, self.y, s, s, self.g.c(self.g.face))
        d.rect(self.x, self.y, s, s, col)
        if self._value:
            d.line(self.x + 2, self.y + s // 2, self.x + s // 3, self.y + s - 3, col)
            d.line(self.x + s // 3, self.y + s - 3, self.x + s - 2, self.y + 2, col)
        fw, fh = self.g.metrics(self.font)
        d.text(self.label, self.x + s + 6, self.y + (s - fh) // 2, col, font=self.font)

    def release(self, px, py):
        if self.hit(px, py):
            self._value = 0 if self._value else 1
            self.redraw()
            self._fire()


class Radio(Control):
    def __init__(self, g, x, y, r, label, group, fg, font):
        fw, fh = g.metrics(font)
        super().__init__(g, x, y, 2 * r + 6 + len(label) * fw, max(2 * r, fh), fg, BG, font)
        self.r = r
        self.label = label
        self.group = group

    def draw(self):
        d = self.g.d
        cx, cy = self.x + self.r, self.y + self.h // 2
        col = self._c(self.fg)
        d.ellipse(cx, cy, self.r, self.r, self.g.c(self.g.face), True)
        d.ellipse(cx, cy, self.r, self.r, col)
        if self._value:
            d.ellipse(cx, cy, max(1, self.r - 3), max(1, self.r - 3), col, True)
        fw, fh = self.g.metrics(self.font)
        d.text(self.label, self.x + 2 * self.r + 6, cy - fh // 2, col, font=self.font)

    def release(self, px, py):
        if self.hit(px, py) and not self._value:
            for c in self.g.controls:
                if isinstance(c, Radio) and c.group == self.group and c is not self:
                    if c._value:
                        c._value = 0
                        c.redraw()
            self._value = 1
            self.redraw()
            self._fire()


class LED(Control):
    interactive = False

    def __init__(self, g, x, y, r, label, colour, font):
        fw, fh = g.metrics(font)
        super().__init__(g, x, y, 2 * r + 6 + len(label) * fw, max(2 * r, fh), colour, BG, font)
        self.r = r
        self.label = label
        self.colour = colour

    def draw(self):
        d = self.g.d
        cx, cy = self.x + self.r, self.y + self.h // 2
        lit = self.colour if self._value else _scale(self.colour, 0.3)
        d.ellipse(cx, cy, self.r, self.r, self.g.c(lit), True)
        d.ellipse(cx, cy, self.r, self.r, self.g.c(_scale(self.colour, 0.6)))
        fw, fh = self.g.metrics(self.font)
        d.text(self.label, self.x + 2 * self.r + 6, cy - fh // 2,
               self.g.c(FG), font=self.font)


# --- gauges -----------------------------------------------------------------
class Gauge(Control):
    """A circular gauge (270 deg, open at the bottom). Updates incrementally: a
    value change repaints only the wedge that changed, not the whole gauge, so
    there is no flicker. The trick is a fixed grid of boundary vertices shared by
    the value arc and the track arc -- because both are built from the same
    points, repainting a sub-range covers exactly the pixels it needs to, with no
    stray fill-colour outline and no seams (each contiguous colour run is one
    poly fill)."""

    interactive = False
    _A0 = 225   # start angle (deg, 0=up clockwise) -> lower-left
    _SWEEP = 270

    def __init__(self, g, x, y, r, fg, lo, hi, font):
        super().__init__(g, x - r, y - r, 2 * r, 2 * r, fg if fg is not None else ACCENT, BG, font)
        self.cx = x
        self.cy = y
        self.r = r
        self.lo = lo
        self.hi = hi
        self._value = lo
        self._built = False

    def _build(self):
        import math

        th = max(3, self.r // 4)
        self.ri = self.r - th
        self.ro = self.r
        # segment count -> ~2px outer chords, so the arc looks smooth
        arclen = math.radians(self._SWEEP) * self.ro
        self.n = max(12, min(180, int(arclen / 2)))
        self._ox = []
        self._oy = []
        self._ix = []
        self._iy = []
        for i in range(self.n + 1):
            a = math.radians(self._A0 + self._SWEEP * i / self.n)
            s = math.sin(a)
            c = math.cos(a)
            self._ox.append(round(self.cx + self.ro * s))
            self._oy.append(round(self.cy - self.ro * c))
            self._ix.append(round(self.cx + self.ri * s))
            self._iy.append(round(self.cy - self.ri * c))
        self._built = True

    def _k(self, val):
        span = self.hi - self.lo
        frac = (val - self.lo) / span if span else 0
        frac = 0 if frac < 0 else 1 if frac > 1 else frac
        return int(round(frac * self.n))

    def _range(self, i, j, colour):
        # Fill the annular sector between grid boundaries i and j as one polygon
        # (outer arc forward, inner arc back) -- one fill, so no internal seams.
        if j <= i:
            return
        import array

        pts = array.array("h")
        for k in range(i, j + 1):
            pts.append(self._ox[k])
            pts.append(self._oy[k])
        for k in range(j, i - 1, -1):
            pts.append(self._ix[k])
            pts.append(self._iy[k])
        self.g.d.poly(0, 0, pts, colour, True)

    def _draw_number(self):
        d = self.g.d
        fw, fh = self.g.metrics(self.font)
        s = str(int(self._value))
        # clear a thin band across the centre (inside the inner disc, so the ring
        # is untouched), then draw the value
        d.fill_rect(self.cx - (self.ri - 2), self.cy - fh // 2, 2 * (self.ri - 2), fh,
                    self.g.c(self.g.face))
        d.text(s, self.cx - len(s) * fw // 2, self.cy - fh // 2, self.g.c(FG), font=self.font)

    def draw(self):
        if not self._built:
            self._build()
        k = self._k(self._value)
        self._range(0, k, self._c(self.fg))          # value arc
        self._range(k, self.n, self.g.c(TRACK))       # track
        # clear the centre disc (never covered by the ring) then the value
        self.g.d.ellipse(self.cx, self.cy, self.ri - 1, self.ri - 1,
                         self.g.c(self.g.face), True)
        self._draw_number()

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, v):
        old = self._value
        self._value = v
        if self.hidden:
            return
        if not self._built:
            self.draw()
            return
        ok = self._k(old)
        nk = self._k(v)
        if nk > ok:                                   # grew -> paint fg over track
            self._range(ok, nk, self._c(self.fg))
        elif nk < ok:                                 # shrank -> paint track over fg
            self._range(nk, ok, self.g.c(TRACK))
        if int(old) != int(v):
            self._draw_number()


class BarGauge(Control):
    """A bar gauge (horizontal if w>=h, else vertical). Like the gauge it updates
    incrementally -- a value change repaints only the strip between the old and
    new fill level, never the whole bar, so it doesn't flash."""

    interactive = False

    def __init__(self, g, x, y, w, h, fg, lo, hi, font):
        super().__init__(g, x, y, w, h, fg if fg is not None else ACCENT, TRACK, font)
        self.lo = lo
        self.hi = hi
        self._value = lo
        self.horiz = w >= h
        self._drawn = False

    def _len(self, val):
        span = self.hi - self.lo
        frac = (val - self.lo) / span if span else 0
        frac = 0 if frac < 0 else 1 if frac > 1 else frac
        return int(((self.w if self.horiz else self.h) - 2) * frac)

    def _fill(self, a, b, colour):
        # paint the strip between fill-lengths a and b (a < b) in colour
        if b <= a:
            return
        d = self.g.d
        if self.horiz:
            d.fill_rect(self.x + 1 + a, self.y + 1, b - a, self.h - 2, colour)
        else:  # vertical fills bottom-up
            d.fill_rect(self.x + 1, self.y + self.h - 1 - b, self.w - 2, b - a, colour)

    def draw(self):
        self.g.d.rect(self.x, self.y, self.w, self.h, self.g.c(FRAME))
        ext = (self.w if self.horiz else self.h) - 2
        length = self._len(self._value)
        self._fill(0, length, self._c(self.fg))       # filled part
        self._fill(length, ext, self.g.c(self.bg))    # track part
        self._drawn = True

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, v):
        old = self._value
        self._value = v
        if self.hidden:
            return
        if not self._drawn:
            self.draw()
            return
        a = self._len(old)
        b = self._len(v)
        if b > a:                                     # grew -> extend fill
            self._fill(a, b, self._c(self.fg))
        elif b < a:                                   # shrank -> track over it
            self._fill(b, a, self.g.c(self.bg))


# --- slider -----------------------------------------------------------------
class Slider(Control):
    def __init__(self, g, x, y, w, h, fg, lo, hi, font):
        super().__init__(g, x, y, w, h, fg if fg is not None else ACCENT, TRACK, font)
        self.lo = lo
        self.hi = hi
        self._value = lo
        self.horiz = w >= h

    def _tr(self):
        # Thumb radius, kept just inside the control's box (on the short axis) so
        # that erasing the box on redraw fully covers the previous thumb -- no
        # remnant is left as it slides.
        return max(3, ((self.h if self.horiz else self.w) - 1) // 2)

    def _thumb(self):
        frac = (self._value - self.lo) / (self.hi - self.lo) if self.hi != self.lo else 0
        frac = 0 if frac < 0 else 1 if frac > 1 else frac
        t = self._tr()
        if self.horiz:
            cx = self.x + t + int((self.w - 2 * t) * frac)
            return cx, self.y + self.h // 2, t
        cy = self.y + self.h - t - int((self.h - 2 * t) * frac)
        return self.x + self.w // 2, cy, t

    def draw(self):
        d = self.g.d
        self.g._erase(self)
        if self.horiz:
            gy = self.y + self.h // 2
            d.fill_rect(self.x, gy - 2, self.w, 4, self.g.c(TRACK))
        else:
            gx = self.x + self.w // 2
            d.fill_rect(gx - 2, self.y, 4, self.h, self.g.c(TRACK))
        cx, cy, t = self._thumb()
        d.ellipse(cx, cy, t, t, self._c(self.fg), True)
        d.ellipse(cx, cy, t, t, self.g.c(FRAME))

    def _set_from(self, px, py):
        t = self._tr()
        if self.horiz:
            span = self.w - 2 * t
            frac = (px - (self.x + t)) / span if span else 0
        else:
            span = self.h - 2 * t
            frac = ((self.y + self.h - t) - py) / span if span else 0
        frac = 0 if frac < 0 else 1 if frac > 1 else frac
        v = self.lo + (self.hi - self.lo) * frac
        v = int(round(v))
        if v != self._value:
            self._value = v
            self.redraw()
            self._fire()

    def press(self, px, py):
        self._set_from(px, py)

    def drag(self, px, py):
        self._set_from(px, py)


# --- text / number boxes ----------------------------------------------------
class TextBox(Control):
    editable = True
    numeric = False

    def __init__(self, g, x, y, w, h, fg, bg, font):
        super().__init__(g, x, y, w, h, fg, bg if bg is not None else 0x303030, font)
        self._value = ""

    def draw(self):
        d = self.g.d
        d.fill_rect(self.x, self.y, self.w, self.h, self._c(self.bg))
        d.rect(self.x, self.y, self.w, self.h, self.g.c(FRAME))
        fw, fh = self.g.metrics(self.font)
        s = str(self._value)
        maxc = max(0, (self.w - 8) // fw)
        if len(s) > maxc:
            s = s[-maxc:]
        tx = self.x + 4
        ty = self.y + (self.h - fh) // 2
        d.text(s, tx, ty, self._c(self.fg), font=self.font)


class NumberBox(TextBox):
    numeric = True

    @property
    def number(self):
        try:
            return float(self._value)
        except ValueError:
            return 0.0


# --- on-screen keyboard / keypad (for touch text entry) ---------------------
# A modal, docked to the bottom of the screen. Alpha layout for text boxes, a
# numeric keypad for number boxes. It also accepts a real USB keyboard while
# open, so either input works. run() returns the edited string, or None if the
# user cancelled.
_ALPHA = [
    list("1234567890"),
    list("qwertyuiop"),
    list("asdfghjkl"),
    list("zxcvbnm,."),
    ["Shift", "Space", "Del", "OK", "Esc"],
]
_NUMPAD = [
    ["7", "8", "9"],
    ["4", "5", "6"],
    ["1", "2", "3"],
    ["0", ".", "-"],
    ["Del", "OK", "Esc"],
]
_PANEL = 0x282828
_KEY = 0x505050


class _OnScreenKeyboard:
    def __init__(self, g, numeric, initial):
        self.g = g
        self.d = g.d
        self.numeric = numeric
        self.text = str(initial)
        self.shift = False
        self.rows = _NUMPAD if numeric else _ALPHA
        sw, sh = hdmi.width(), hdmi.height()
        self.w = sw
        self.x = 0
        self.title_h = 22
        self.key_h = min(34, (sh // 2 - self.title_h) // len(self.rows))
        if self.key_h < 14:
            self.key_h = 14
        self.h = self.title_h + self.key_h * len(self.rows)
        self.y = sh - self.h

    def _draw_title(self):
        d = self.d
        d.fill_rect(self.x, self.y, self.w, self.title_h, self.g.c(_PANEL))
        d.text(self.text + "_", self.x + 6, self.y + (self.title_h - 12) // 2,
               self.g.c(FG), font=1)

    def draw(self):
        d = self.d
        d.fill_rect(self.x, self.y, self.w, self.h, self.g.c(_PANEL))
        d.rect(self.x, self.y, self.w, self.h, self.g.c(FRAME))
        self._draw_title()
        for ri, row in enumerate(self.rows):
            kw = self.w // len(row)
            ky = self.y + self.title_h + ri * self.key_h
            for ci, label in enumerate(row):
                kx = self.x + ci * kw
                face = ACCENT if (label == "Shift" and self.shift) else _KEY
                d.rbox(kx + 1, ky + 1, kw - 2, self.key_h - 2, 3,
                       self.g.c(FRAME), fill=self.g.c(face))
                lab = label.upper() if (self.shift and len(label) == 1 and label.isalpha()) else label
                tx = kx + (kw - len(lab) * 8) // 2
                ty = ky + (self.key_h - 12) // 2
                d.text(lab, tx, ty, self.g.c(FG), font=1)

    def _key_at(self, x, y):
        ty = self.y + self.title_h
        if y < ty or y >= self.y + self.h or x < self.x or x >= self.x + self.w:
            return None
        ri = (y - ty) // self.key_h
        if ri >= len(self.rows):
            return None
        row = self.rows[ri]
        ci = (x - self.x) // (self.w // len(row))
        if ci < 0 or ci >= len(row):
            return None
        return row[ci]

    def _press(self, label):
        if label == "Shift":
            self.shift = not self.shift
            self.draw()
            return None
        if label == "Space":
            self.text += " "
        elif label == "Del":
            self.text = self.text[:-1]
        elif label == "OK":
            return "ok"
        elif label == "Esc":
            return "cancel"
        else:
            ch = label.upper() if (self.shift and label.isalpha()) else label
            if self.numeric and ch not in "0123456789.+-":
                return None
            self.text += ch
        return None

    def _feed(self, code):
        if code == 10 or code == 13:
            return "ok"
        if code == 27:
            return "cancel"
        if code == 8 or code == 0x7F:
            self.text = self.text[:-1]
        elif 32 <= code <= 126:
            ch = chr(code)
            if self.numeric and ch not in "0123456789.+-":
                return None
            self.text += ch
        return None

    def _wait_release(self):
        while self.g._pointer()[2]:
            time.sleep_ms(10)

    def run(self):
        self.draw()
        self.g._keys = []
        self.g.focus = self  # truthy, so keyboard.on_key buffers physical keys
        prev_down = False
        try:
            while True:
                while self.g._keys:
                    act = self._feed(self.g._keys.pop(0))
                    if act:
                        self._wait_release()
                        return self.text if act == "ok" else None
                    self._draw_title()
                x, y, down = self.g._pointer()
                if down and not prev_down:
                    k = self._key_at(x, y)
                    if k is not None:
                        act = self._press(k)
                        if act:
                            self._wait_release()
                            return self.text if act == "ok" else None
                        self._draw_title()
                prev_down = down
                time.sleep_ms(15)
        finally:
            self.g.focus = None


# --- the GUI manager --------------------------------------------------------
class GUI:
    """Owns a set of controls, draws them, and dispatches pointer + keyboard
    events. Pass a Display (default hdmi.fb()); call poll() from your loop."""

    def __init__(self, display=None, face=None):
        self.d = display or hdmi.fb()
        self.face = pcgfx.BLACK if face is None else face  # background behind controls
        self.controls = []
        self.focus = None
        self._down = False
        self._active = None
        self._px = 0        # last pointer position while pressed (for release)
        self._py = 0
        self._keys = []
        self._started = False
        # cache font metrics: {font_number: (w, h)}
        self._fm = {}
        for entry in hdmi.fonts():
            self._fm[entry[0]] = (entry[1], entry[2])

    # colour + metrics helpers used by controls
    def c(self, rgb):
        return self.d.colour(rgb)

    def metrics(self, font):
        return self._fm.get(font or 1, (8, 12))

    # --- lifecycle ---
    def start(self):
        """Begin capturing the USB keyboard for text boxes."""
        keyboard.on_key(self._on_key)
        self._started = True

    def stop(self):
        if self._started:
            keyboard.on_key()
            self._started = False

    def cls(self):
        """Clear the screen to the face colour and redraw every control."""
        hdmi.fill(self.c(self.face))
        self.redraw()

    def redraw(self):
        for ctrl in self.controls:
            if not ctrl.hidden:
                ctrl.draw()

    def _erase(self, ctrl):
        self.d.fill_rect(ctrl.x, ctrl.y, ctrl.w, ctrl.h, self.c(self.face))

    def remove(self, ctrl):
        if ctrl in self.controls:
            self._erase(ctrl)
            self.controls.remove(ctrl)
            if self.focus is ctrl:
                self.focus = None

    # --- keyboard (scheduled from the USB driver) ---
    # A physical keypress is buffered only while a modal keyboard is open (which
    # sets self.focus); it then drains the codes itself. self.focus is unused
    # otherwise.
    def _on_key(self, code):
        if self.focus is not None:
            self._keys.append(code)

    def _redraw_region(self, x, y, w, h):
        """Repaint a rectangle to the face colour and redraw every control that
        overlaps it (used to restore the screen after a modal keyboard closes)."""
        self.d.fill_rect(x, y, w, h, self.c(self.face))
        for ctrl in self.controls:
            if ctrl.hidden:
                continue
            if not (ctrl.x + ctrl.w <= x or ctrl.x >= x + w or
                    ctrl.y + ctrl.h <= y or ctrl.y >= y + h):
                ctrl.draw()

    def _edit(self, ctrl):
        """Open the on-screen keyboard/keypad for a text/number box (touch)."""
        kb = _OnScreenKeyboard(self, getattr(ctrl, "numeric", False), ctrl._value)
        result = kb.run()
        self._redraw_region(kb.x, kb.y, kb.w, kb.h)
        self._down = False
        self._active = None
        if result is not None:
            ctrl._value = result
            ctrl.redraw()
            ctrl._fire()

    # --- pointer ---
    def _pointer(self):
        # Prefer an active touch; otherwise the mouse (whose cursor is always
        # positioned). Returns (x, y, down).
        import touch as _touch
        import mouse as _mouse

        if _touch.query("PRESENT") and _touch.query("DOWN"):
            return _touch.query("X"), _touch.query("Y"), True
        if _mouse.query("PRESENT"):
            return _mouse.query("X"), _mouse.query("Y"), bool(_mouse.query("L"))
        return 0, 0, False

    def _at(self, x, y):
        for ctrl in reversed(self.controls):
            if ctrl.interactive and not ctrl.hidden and ctrl.enabled and ctrl.hit(x, y):
                return ctrl
        return None

    def poll(self):
        """Read the pointer once and dispatch events. Call often."""
        x, y, down = self._pointer()
        if down:
            # Remember where the pointer is while it's down: on release the
            # source reports "not down" and its coordinates go stale (a lifted
            # touch reads -1), so the release must use the last live position.
            self._px, self._py = x, y
        if down and not self._down:            # press edge
            self._down = True
            c = self._at(x, y)
            self._active = c
            if c is not None:
                c.press(x, y)
        elif down and self._down:              # drag
            if self._active is not None:
                self._active.drag(x, y)
        elif (not down) and self._down:        # release edge
            self._down = False
            c = self._active
            self._active = None
            if c is not None:
                if c.editable:
                    # tapping a text/number box pops up the on-screen keyboard
                    if c.hit(self._px, self._py):
                        self._edit(c)
                else:
                    c.release(self._px, self._py)

    # --- control factories (create, register, draw, return) ---
    def _add(self, ctrl):
        self.controls.append(ctrl)
        if not ctrl.hidden:
            ctrl.draw()
        return ctrl

    def caption(self, x, y, text, fg=None, bg=None, font=1, just="LT"):
        return self._add(Caption(self, x, y, text, fg, bg, font, just))

    def frame(self, x, y, w, h, title="", fg=None, font=1):
        return self._add(Frame(self, x, y, w, h, title, fg, font))

    def button(self, x, y, w, h, text, fg=None, bg=None, font=1, callback=None):
        b = Button(self, x, y, w, h, text, fg, bg, font)
        b.callback = callback
        return self._add(b)

    def switch(self, x, y, w, h, labels="ON|OFF", value=0, fg=None, bg=None,
               font=1, callback=None):
        s = Switch(self, x, y, w, h, labels, fg, bg, font)
        s.callback = callback
        s._value = value
        return self._add(s)

    def checkbox(self, x, y, size, label, value=0, fg=None, font=1, callback=None):
        c = CheckBox(self, x, y, size, label, fg, font)
        c.callback = callback
        c._value = value
        return self._add(c)

    def radio(self, x, y, r, label, group=0, value=0, fg=None, font=1, callback=None):
        c = Radio(self, x, y, r, label, group, fg, font)
        c.callback = callback
        c._value = value
        return self._add(c)

    def led(self, x, y, r, label="", colour=pcgfx.GREEN, value=0, font=1):
        c = LED(self, x, y, r, label, colour, font)
        c._value = value
        return self._add(c)

    def gauge(self, x, y, r, value=0, lo=0, hi=100, fg=None, font=1):
        c = Gauge(self, x, y, r, fg, lo, hi, font)
        c._value = value
        return self._add(c)

    def bargauge(self, x, y, w, h, value=0, lo=0, hi=100, fg=None, font=1):
        c = BarGauge(self, x, y, w, h, fg, lo, hi, font)
        c._value = value
        return self._add(c)

    def slider(self, x, y, w, h, value=0, lo=0, hi=100, fg=None, font=1, callback=None):
        c = Slider(self, x, y, w, h, fg, lo, hi, font)
        c.callback = callback
        c._value = value
        return self._add(c)

    def textbox(self, x, y, w, h, text="", fg=None, bg=None, font=1, callback=None):
        t = TextBox(self, x, y, w, h, fg, bg, font)
        t.callback = callback
        t._value = text
        return self._add(t)

    def numberbox(self, x, y, w, h, value="", fg=None, bg=None, font=1, callback=None):
        t = NumberBox(self, x, y, w, h, fg, bg, font)
        t.callback = callback
        t._value = str(value)
        return self._add(t)
