# Turtle graphics for the Pico Computer 3 (ported from MMBasic's TURTLE).
#
#   t = Turtle()            # draws on the HDMI screen (hdmi.fb())
#   t.reset()               # clear the screen; turtle at the centre, facing up
#   for _ in range(4):
#       t.forward(80); t.right(90)     # a square
#
# Conventions (as MMBasic): coordinates are screen pixels, HOME is the screen
# centre, and the heading is 0 = up, 90 = right, i.e. clockwise (so right()
# turns clockwise). Colours are 24-bit RGB (use the palette names RED, WHITE, …
# or 0xRRGGBB); they're converted to the current display format automatically.
#
# Drawing goes to the current write target's Display (hdmi.fb()); after a
# screen() mode change, make a new Turtle (the pixel format differs per mode).

import math

import hdmi


class Turtle:
    def __init__(self, display=None):
        self.d = display or hdmi.fb()
        self._stack = []
        self._reset_state()

    def _reset_state(self):
        self.x = hdmi.width() / 2
        self.y = hdmi.height() / 2
        self.h = 0.0            # heading: 0 = up, 90 = right (clockwise)
        self._down = True
        self._pen = self.d.colour(0xFFFFFF)
        self._pw = 1
        self._fill = self.d.colour(0xFFFFFF)
        self._filling = False
        self._poly = None       # points recorded between begin_fill/end_fill

    def reset(self, clear=True):
        """Reset the turtle to the centre facing up, pen down, white. By
        default also clears the screen (clear=False keeps the drawing)."""
        if clear:
            self.d.fill(0)
        self._stack = []
        self._reset_state()

    # --- Movement --------------------------------------------------------
    def forward(self, dist):
        rad = math.radians(self.h)
        nx = self.x + dist * math.sin(rad)
        ny = self.y - dist * math.cos(rad)
        if self._down:
            self.d.line(round(self.x), round(self.y), round(nx), round(ny),
                        self._pen, self._pw)
        self.x = nx
        self.y = ny
        if self._poly is not None:
            self._poly.append((round(nx), round(ny)))

    fd = forward

    def backward(self, dist):
        self.forward(-dist)

    back = backward
    bk = backward

    def right(self, angle=90):
        """Turn clockwise by `angle` degrees (default 90)."""
        self.h = (self.h + angle) % 360

    rt = right

    def left(self, angle=90):
        """Turn counter-clockwise by `angle` degrees (default 90)."""
        self.h = (self.h - angle) % 360

    lt = left

    def goto(self, x, y):
        """Move to absolute screen pixel (x, y), drawing if the pen is down."""
        if self._down:
            self.d.line(round(self.x), round(self.y), round(x), round(y),
                        self._pen, self._pw)
        self.x = x
        self.y = y
        if self._poly is not None:
            self._poly.append((round(x), round(y)))

    setpos = goto
    setposition = goto
    move = goto

    def setx(self, x):
        self.goto(x, self.y)

    def sety(self, y):
        self.goto(self.x, y)

    def setheading(self, angle):
        """Set the absolute heading (0 = up, 90 = right, clockwise)."""
        self.h = angle % 360

    seth = setheading

    def home(self):
        """Move to the screen centre and face up (drawing if the pen is down)."""
        self.goto(hdmi.width() / 2, hdmi.height() / 2)
        self.h = 0.0

    def position(self):
        return (self.x, self.y)

    pos = position

    def xcor(self):
        return self.x

    def ycor(self):
        return self.y

    def heading(self):
        return self.h

    def isdown(self):
        return self._down

    # --- Pen -------------------------------------------------------------
    def penup(self):
        self._down = False

    pu = penup

    def pendown(self):
        self._down = True

    pd = pendown

    def pencolor(self, rgb):
        """Set the pen colour (24-bit RGB, e.g. RED or 0xRRGGBB)."""
        self._pen = self.d.colour(rgb)

    pencolour = pencolor
    color = pencolor
    colour = pencolor

    def pensize(self, w):
        """Set the pen line width in pixels (1..50)."""
        self._pw = max(1, min(50, int(w)))

    width = pensize

    # --- Arcs and curves -------------------------------------------------
    def arc(self, radius, angle):
        """Move along a circular arc of `radius`, turning `angle` degrees in
        total (positive = clockwise, like right()). The turtle ends turned."""
        segs = max(4, int(abs(angle) / 5) + 1)
        step = angle / segs
        dist = 2 * radius * math.sin(math.radians(step) / 2)
        for _ in range(segs):
            self.forward(dist)
            self.h = (self.h + step) % 360

    def bezier(self, cp1_dist, cp1_angle, cp2_dist, cp2_angle, end_dist, end_angle):
        """Draw a cubic Bezier from the turtle: the two control points and the
        end point are each given as a (distance, angle) offset from the turtle's
        current position and heading (as MMBasic). The turtle moves to the end."""
        def pt(dist, ang):
            r = math.radians(self.h + ang)
            return (round(self.x + dist * math.sin(r)),
                    round(self.y - dist * math.cos(r)))

        p0 = (round(self.x), round(self.y))
        p3 = pt(end_dist, end_angle)
        if self._down:
            self.d.bezier([p0, pt(cp1_dist, cp1_angle), pt(cp2_dist, cp2_angle), p3],
                          self._pen)
        self.x, self.y = p3
        if self._poly is not None:
            self._poly.append(p3)

    # --- Shapes (drawn at the current position) --------------------------
    def circle(self, radius):
        """A circle outline centred on the turtle (does not move it)."""
        self.d.ellipse(round(self.x), round(self.y), int(radius), int(radius), self._pen)

    def dot(self, size=5):
        """A filled dot of diameter ~`size` at the turtle."""
        r = max(1, int(size) // 2)
        self.d.ellipse(round(self.x), round(self.y), r, r, self._pen, True)

    def fcircle(self, radius):
        """A filled circle (fill colour) + outline (if pen down) at the turtle."""
        if self._filling:
            self.d.ellipse(round(self.x), round(self.y), int(radius), int(radius),
                           self._fill, True)
        if self._down:
            self.d.ellipse(round(self.x), round(self.y), int(radius), int(radius),
                           self._pen)

    def rectangle(self, w, h):
        """A rectangle centred on the turtle: filled (if a fill colour is set)
        and outlined (if the pen is down)."""
        x = round(self.x - w / 2)
        y = round(self.y - h / 2)
        if self._filling:
            self.d.fill_rect(x, y, int(w), int(h), self._fill)
        if self._down:
            self.d.rect(x, y, int(w), int(h), self._pen)

    frectangle = rectangle
    frect = rectangle

    def wedge(self, radius, start, end):
        """A filled pie slice / sector at the turtle, from angle `start` to
        `end` degrees (0 = up, clockwise)."""
        self.d.arc(round(self.x), round(self.y), 0, int(radius), start, end,
                   self._fill if self._filling else self._pen)

    def stamp(self, size=12, colour=None):
        """Stamp a small triangle at the turtle, pointing in its heading."""
        import array

        col = self._pen if colour is None else self.d.colour(colour)

        def pt(dist, ang):
            r = math.radians(self.h + ang)
            return (round(self.x + dist * math.sin(r)),
                    round(self.y - dist * math.cos(r)))

        a = pt(size, 0)            # tip (forward)
        b = pt(size * 0.7, 140)    # rear corners
        c = pt(size * 0.7, -140)
        self.d.poly(0, 0, array.array("h", (a[0], a[1], b[0], b[1], c[0], c[1])),
                    col, True)

    # --- Fill ------------------------------------------------------------
    def fillcolor(self, rgb):
        """Set the fill colour and enable filling (as MMBasic FILL COLOUR)."""
        self._fill = self.d.colour(rgb)
        self._filling = True

    fillcolour = fillcolor

    def nofill(self):
        self._filling = False

    def begin_fill(self):
        """Start recording the turtle's path as a polygon to fill."""
        self._poly = [(round(self.x), round(self.y))]

    bf = begin_fill

    def end_fill(self):
        """Fill the polygon traced since begin_fill() with the fill colour."""
        pts = self._poly
        self._poly = None
        if pts and len(pts) > 2:
            import array

            flat = array.array("h")
            for px, py in pts:
                flat.append(px)
                flat.append(py)
            self.d.poly(0, 0, flat, self._fill, True)

    ef = end_fill

    # --- State stack -----------------------------------------------------
    def push(self):
        """Save the current position and heading."""
        self._stack.append((self.x, self.y, self.h))

    def pop(self):
        """Restore the last saved position and heading (no drawing)."""
        if self._stack:
            self.x, self.y, self.h = self._stack.pop()
