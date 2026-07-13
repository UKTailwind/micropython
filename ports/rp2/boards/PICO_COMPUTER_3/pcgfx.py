# Pico Computer 3 graphics helpers: the MMBasic named-colour palette (as 24-bit
# RGB888) plus a Display class that subclasses framebuf.FrameBuffer and converts
# RGB888 colours to the framebuffer's actual pixel format (RGB332 or RGB565).
#
# hdmi.fb() returns a Display, so:
#     fb = hdmi.fb()
#     fb.text("PICO COMPUTER 3", 8, 8, fb.colour(255, 0, 0))
#     fb.text("PICO COMPUTER 3", 8, 8, fb.colour(RED))   # RED injected by _boot

import framebuf

# MMBasic 16-colour palette (RGB888).
BLACK = 0x000000
BLUE = 0x0000FF
MYRTLE = 0x004000
COBALT = 0x0040FF
MIDGREEN = 0x008000
CERULEAN = 0x0080FF
GREEN = 0x00FF00
CYAN = 0x00FFFF
RED = 0xFF0000
MAGENTA = 0xFF00FF
RUST = 0xFF4000
FUCHSIA = 0xFF40FF
BROWN = 0xFF8000
LILAC = 0xFF80FF
YELLOW = 0xFFFF00
WHITE = 0xFFFFFF

# Extras.
GRAY = 0x808080
LITEGRAY = 0xD2D2D2
ORANGE = 0xFFA500
PINK = 0xFFA0AB
GOLD = 0xFFD700
SALMON = 0xFA8072


class Display(framebuf.FrameBuffer):
    """A framebuf.FrameBuffer whose colour() method converts RGB888 to the
    framebuffer's real pixel format, so drawing code can stay resolution-agnostic."""

    def __init__(self, buffer, width, height, fmt):
        super().__init__(buffer, width, height, fmt)
        self._is332 = fmt == framebuf.GS8
        self._is121 = fmt == framebuf.GS4_HMSB

    # --- Richer 2D primitives (framebuf lacks these) ----------------------
    # Colours are native-format, like the framebuf methods: pass fb.colour(RED).

    def line(self, x1, y1, x2, y2, colour, w=1):
        """A line, optionally `w` pixels wide (framebuf's line is 1px only).
        A thick line is drawn as a filled quadrilateral (butt caps)."""
        if w <= 1:
            return super().line(x1, y1, x2, y2, colour)
        import math
        import array

        dx = x2 - x1
        dy = y2 - y1
        length = math.sqrt(dx * dx + dy * dy)
        if length < 1:
            return self.fill_rect(x1 - w // 2, y1 - w // 2, w, w, colour)
        # Perpendicular unit vector * half-width -> the 4 corners of the band.
        ox = -dy / length * w / 2
        oy = dx / length * w / 2
        pts = array.array("h", (
            round(x1 + ox), round(y1 + oy), round(x2 + ox), round(y2 + oy),
            round(x2 - ox), round(y2 - oy), round(x1 - ox), round(y1 - oy),
        ))
        self.poly(0, 0, pts, colour, True)

    def rbox(self, x, y, w, h, r, colour, fill=None):
        """A rounded rectangle: top-left (x,y), size w x h, corner radius r,
        `colour` outline, optional `fill`. r is clamped to half the shorter
        side (as MMBasic RBOX)."""
        r = min(r, w // 2, h // 2)
        if r < 1:  # no rounding possible -> plain rectangle
            if fill is not None:
                self.fill_rect(x, y, w, h, fill)
            return self.rect(x, y, w, h, colour)
        xl = x + r
        xr = x + w - 1 - r
        yt = y + r
        yb = y + h - 1 - r
        if fill is not None:
            self.fill_rect(x, yt, w, h - 2 * r, fill)          # centre band
            self.fill_rect(xl, y, w - 2 * r, r, fill)          # top strip
            self.fill_rect(xl, y + h - r, w - 2 * r, r, fill)  # bottom strip
            self.ellipse(xl, yt, r, r, fill, True, 2)          # corners (quadrant masks)
            self.ellipse(xr, yt, r, r, fill, True, 1)
            self.ellipse(xl, yb, r, r, fill, True, 4)
            self.ellipse(xr, yb, r, r, fill, True, 8)
        self.hline(xl, y, w - 2 * r, colour)
        self.hline(xl, y + h - 1, w - 2 * r, colour)
        self.vline(x, yt, h - 2 * r, colour)
        self.vline(x + w - 1, yt, h - 2 * r, colour)
        self.ellipse(xl, yt, r, r, colour, False, 2)
        self.ellipse(xr, yt, r, r, colour, False, 1)
        self.ellipse(xl, yb, r, r, colour, False, 4)
        self.ellipse(xr, yb, r, r, colour, False, 8)

    def arc(self, x, y, r1, r2, a1, a2, colour):
        """A filled arc / annular sector centred at (x,y) between inner radius
        r1 and outer radius r2, from angle a1 to a2 degrees (MMBasic ARC:
        0 deg = up, 90 = right, clockwise). a1==a2 (or a2<a1) sweeps a full
        ring. For a thin arc outline use e.g. r1 = r-1, r2 = r."""
        import math

        if r2 < r1:
            r1, r2 = r2, r1
        a1 %= 360
        a2 %= 360
        if a2 < a1:
            a2 += 360
        if a1 == a2:  # equal radials -> a full ring
            a2 = a1 + 360
        sweep = a2 - a1
        sqrt = math.sqrt

        if sweep >= 360:
            # Full ring: two annulus x-spans per row, no angle test needed.
            hl = self.hline
            r1sq = r1 * r1
            r2sq = r2 * r2
            for sy in range(y - r2, y + r2 + 1):
                dy2 = (sy - y) ** 2
                if dy2 > r2sq:
                    continue
                dxo = int(sqrt(r2sq - dy2))
                dxi = int(sqrt(r1sq - dy2)) if r1sq > dy2 else 0
                w = dxo - dxi + 1
                hl(x - dxo, sy, w, colour)  # left span
                hl(x + dxi, sy, w, colour)  # right span
            return

        # Partial sector: fill the annular-sector POLYGON (outer arc forward +
        # inner arc back). One C poly-fill, so no per-pixel angle test -> fast
        # and gap-free. The curved edges are chord-approximated (~2px chords).
        import array

        sin = math.sin
        cos = math.cos
        rad = math.radians
        segs = max(4, min(720, int(rad(sweep) * r2 / 2)))
        outer = []
        for k in range(segs + 1):
            ang = rad(a1 + sweep * k / segs)
            outer.append((round(x + r2 * sin(ang)), round(y - r2 * cos(ang))))
        if r1 > 0:
            inner = []
            for k in range(segs + 1):
                ang = rad(a1 + sweep * k / segs)
                inner.append((round(x + r1 * sin(ang)), round(y - r1 * cos(ang))))
            seq = outer + inner[::-1]
        else:
            inner = None
            seq = outer + [(x, y)]
        flat = array.array("h")
        for px, py in seq:
            flat.append(px)
            flat.append(py)
        self.poly(0, 0, flat, colour, True)
        # A very thin band can lose rows in a polygon fill; stroke its edges.
        if r2 - r1 <= 3:
            for edge in (outer, inner):
                if edge:
                    for i in range(len(edge) - 1):
                        super().line(edge[i][0], edge[i][1],
                                     edge[i + 1][0], edge[i + 1][1], colour)

    def bezier(self, pts, colour):
        """A Bezier curve through the control points `pts` (a list of (x, y),
        2 or more), as connected line segments. MMBasic BEZIER (N-point
        Bernstein)."""
        import math

        n = len(pts)
        if n < 2:
            return
        binom = [1] * n  # C(n-1, i)
        for i in range(1, n):
            binom[i] = binom[i - 1] * (n - i) // i
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        diag = math.sqrt((max(xs) - min(xs)) ** 2 + (max(ys) - min(ys)) ** 2)
        steps = max(10, min(2000, int(diag) * 3))
        px, py = pts[0]
        for k in range(1, steps + 1):
            t = k / steps
            mt = 1 - t
            bx = 0.0
            by = 0.0
            for i in range(n):
                b = binom[i] * (t ** i) * (mt ** (n - 1 - i))
                bx += b * pts[i][0]
                by += b * pts[i][1]
            nx = round(bx)
            ny = round(by)
            super().line(px, py, nx, ny, colour)
            px, py = nx, ny

    def flood(self, x, y, colour, border=None):
        """Flood fill from (x, y). Without `border`: replace the contiguous
        region of the seed pixel's colour with `colour` (paint bucket). With
        `border` (a native colour): fill outward over any colour, stopping at
        the border colour. Operates on the current HDMI write target (so use
        the Display from hdmi.fb())."""
        import hdmi

        hdmi.flood(x, y, colour, -1 if border is None else border)

    def colour(self, r, g=None, b=None):
        """Return an RGB888 colour packed for this display's format.
        colour(0xRRGGBB) or colour(r, g, b). In RGB1024 mode (GS4_HMSB / RGB121
        4-bit) this returns the nearest palette index (bit3=R, bits2:1=G, bit0=B)."""
        if g is None:  # r is a 24-bit RGB888 value
            b = r & 0xFF
            g = (r >> 8) & 0xFF
            r = (r >> 16) & 0xFF
        if self._is121:  # 4-bit: nearest of the 16 palette entries
            return (
                (0x08 if r >= 0x80 else 0)
                | (((g + 42) // 85) << 1)  # G to 0..3 (nearest of 0/85/170/255)
                | (1 if b >= 0x80 else 0)
            )
        if self._is332:  # RGB332
            return (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)
        # RGB565
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

    color = colour  # US spelling alias
