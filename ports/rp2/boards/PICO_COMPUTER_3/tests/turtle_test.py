# Turtle graphics test suite -- a Python port of MMBasic's turtletest.bas.
#
#   run("/sd/turtle_test.py")
#
# Draws each test on the HDMI screen (320x240) and prints the test name to the
# serial console; press any (USB) key to advance, or each step auto-advances
# after ~1.5 s. MMBasic features we don't implement are noted and skipped:
# the animated cursor (SHOW/HIDE TURTLE -- use stamp()) and ARECT (rotated
# rectangle; only axis-aligned rectangle()). Fill PATTERNS (MMBasic TURTLE
# FILL PATTERN -> fillpattern()) are implemented and tested below.

import random
import time

import hdmi
import keyboard
import pcconsole
from pcturtle import Turtle

RED = 0xFF0000
GREEN = 0x00FF00
BLUE = 0x0000FF
YELLOW = 0xFFFF00
MAGENTA = 0xFF00FF
CYAN = 0x00FFFF
WHITE = 0xFFFFFF
BROWN = 0xFF8000


def rgb(r, g, b):
    return (r << 16) | (g << 8) | b


def rnd():
    return random.random()


t = None


_pending = False


def wait(ms=1500):
    t0 = time.ticks_ms()
    keyboard.keydown(0)  # drain
    while keyboard.keydown(0) == 0 and time.ticks_diff(time.ticks_ms(), t0) < ms:
        time.sleep_ms(20)


def _hold():
    # Pause to view the drawing currently on screen (each case draws AFTER
    # announcing its name, so we wait for the PREVIOUS drawing here -- that way
    # the printed name always matches what's displayed).
    global _pending
    if _pending:
        wait()
        _pending = False


def case(name):
    global _pending
    _hold()                # let the previous drawing be viewed first
    print("  -", name)
    _pending = True


def section(name):
    _hold()
    print()
    print("===", name, "===")


# --- 1: Basic movement ------------------------------------------------------
def test_basic_movement():
    section("Basic movement")

    case("Square with forward/right")
    t.reset()
    for _ in range(4):
        t.forward(100)
        t.right(90)

    case("Triangle with forward/left")
    t.reset()
    for _ in range(3):
        t.forward(100)
        t.left(120)

    case("Star pattern")
    t.reset()
    for _ in range(5):
        t.forward(100)
        t.right(144)

    case("Forward then back")
    t.reset()
    t.forward(100)
    time.sleep_ms(400)
    t.back(100)

    case("Spiral (increasing distances)")
    t.reset()
    for i in range(1, 51):
        t.forward(i * 2)
        t.right(15)


# --- 2: Pen control ---------------------------------------------------------
def test_pen_control():
    section("Pen control")

    case("Dashed line (penup/pendown)")
    t.reset()
    for _ in range(5):
        t.pendown()
        t.forward(20)
        t.penup()
        t.forward(20)

    case("Rainbow colours")
    t.reset()
    cols = (RED, rgb(255, 127, 0), YELLOW, GREEN, BLUE, rgb(75, 0, 130), rgb(148, 0, 211))
    for c in cols:
        t.pencolor(c)
        t.forward(40)
        t.right(51)

    case("Varying line widths")
    t.reset()
    for w in range(1, 6):
        t.pensize(w)
        t.forward(50)
        t.right(90)

    case("Coloured thick spiral")
    t.reset()
    t.pensize(3)
    for i in range(1, 37):
        t.pencolor(rgb(255, (i * 7) % 256, 255 - ((i * 7) % 256)))
        t.forward(i * 2)
        t.right(10)


# --- 3: Position ------------------------------------------------------------
def test_position():
    section("Position control")

    case("Box with goto")
    t.reset()
    t.penup()
    t.goto(50, 50)
    t.pendown()
    t.goto(270, 50)
    t.goto(270, 190)
    t.goto(50, 190)
    t.goto(50, 50)

    case("Grid with setx/sety")
    t.reset()
    t.penup()
    for x in range(40, 281, 40):
        t.penup()
        t.setx(x)
        t.sety(40)
        t.pendown()
        t.sety(200)

    case("Radial lines with setheading")
    t.reset()
    for a in range(0, 360, 30):
        t.home()
        t.setheading(a)
        t.forward(80)

    case("Random walk then home")
    t.reset()
    for _ in range(50):
        t.forward(8)
        t.right(rnd() * 90 - 45)
    t.penup()
    t.home()
    t.pendown()
    t.pencolor(RED)
    t.circle(5)


# --- 4: Circles -------------------------------------------------------------
def test_circles():
    section("Circles")

    case("Concentric circles")
    t.reset()
    for r in range(10, 101, 10):
        t.home()
        t.circle(r)

    case("Circle grid")
    t.reset()
    for x in range(60, 261, 50):
        for y in range(60, 181, 40):
            t.penup()
            t.goto(x, y)
            t.pendown()
            t.circle(20)

    case("Dots of varying sizes")
    t.reset()
    for i in range(1, 11):
        t.penup()
        t.goto(30 + i * 25, 120)
        t.pendown()
        t.dot(i * 3)

    case("Filled circles")
    t.reset()
    t.fillcolor(rgb(255, 100, 100))
    t.penup()
    t.goto(100, 120)
    t.fcircle(40)
    t.goto(220, 120)
    t.fillcolor(rgb(100, 100, 255))
    t.fcircle(40)


# --- 5: Rectangles (ARECT/rotated not implemented) --------------------------
def test_rectangles():
    section("Rectangles")

    case("Filled rectangles")
    t.reset()
    t.penup()
    t.fillcolor(RED)
    t.goto(80, 120)
    t.rectangle(60, 40)
    t.fillcolor(GREEN)
    t.goto(160, 120)
    t.rectangle(60, 40)
    t.fillcolor(BLUE)
    t.goto(240, 120)
    t.rectangle(60, 40)

    print("  - (skipped: ARECT rotated rectangles -- not implemented)")


# --- 6: Arcs ----------------------------------------------------------------
def test_arcs():
    section("Arcs")

    case("Semicircle arc")
    t.reset()
    t.arc(50, 180)

    case("Flower with arc petals")
    t.reset()
    for _ in range(30):
        t.arc(40, 60)
        t.right(120)
        t.arc(40, 60)
        t.right(195)

    case("arc left vs right")
    t.reset()
    t.penup()
    t.goto(100, 120)
    t.pendown()
    t.pencolor(RED)
    t.arc(50, 180)          # ARCL
    t.penup()
    t.goto(220, 120)
    t.pendown()
    t.pencolor(BLUE)
    t.arc(50, -180)         # ARCR

    case("Arc spiral")
    t.reset()
    for i in range(1, 21):
        t.arc(i * 3, 30)

    case("Pac-Man with wedge")
    t.reset()
    t.fillcolor(YELLOW)
    t.wedge(60, 30, 330)
    t.penup()
    t.goto(175, 100)
    t.fillcolor(0x000000)
    t.fcircle(5)

    case("Pie chart")
    t.reset()
    angles = (90, 120, 150, 0)
    cols = (RED, GREEN, BLUE, YELLOW)
    start = 0
    for a, c in zip(angles, cols):
        t.home()
        t.fillcolor(c)
        t.wedge(70, start, start + a)
        start += a


# --- 8: Polygon fill --------------------------------------------------------
def test_polygon_fill():
    section("Polygon fill (begin_fill/end_fill)")

    case("Filled triangle")
    t.reset()
    t.fillcolor(rgb(255, 100, 100))
    t.begin_fill()
    for _ in range(3):
        t.forward(100)
        t.right(120)
    t.end_fill()

    case("Filled star")
    t.reset()
    t.fillcolor(YELLOW)
    t.begin_fill()
    for _ in range(5):
        t.forward(100)
        t.right(144)
    t.end_fill()

    case("Filled hexagon")
    t.reset()
    t.fillcolor(rgb(100, 200, 255))
    t.begin_fill()
    for _ in range(6):
        t.forward(60)
        t.right(60)
    t.end_fill()

    case("Polygon traced with arcs")
    t.reset()
    t.fillcolor(rgb(200, 100, 200))
    t.begin_fill()
    for _ in range(4):
        t.arc(40, 90)
    t.end_fill()

    case("Pattern fills (MMBasic textures)")
    t.reset()
    x = 25
    for p in (1, 4, 6, 13, 18, 28):
        t.penup()
        t.goto(x, 100)
        t.pendown()
        t.right(90)
        t.fillcolor(YELLOW)
        t.fillpattern(p)
        t.begin_fill()
        for _ in range(4):
            t.forward(45)
            t.right(90)
        t.end_fill()
        t.setheading(0)
        x += 50
    t.fillpattern(0)


# --- 9: Stack (push/pop) ----------------------------------------------------
def _branch(length):
    if length < 10:
        return
    t.forward(length)
    t.push()
    t.left(30)
    _branch(length * 0.7)
    t.pop()
    t.push()
    t.right(30)
    _branch(length * 0.7)
    t.pop()


def _snowflake_branch(length):
    t.forward(length)
    t.push()
    t.right(45)
    t.forward(length * 0.4)
    t.pop()
    t.push()
    t.left(45)
    t.forward(length * 0.4)
    t.pop()


def test_stack():
    section("Stack (push/pop)")

    case("Basic push/pop")
    t.reset()
    t.push()
    t.forward(100)
    t.right(90)
    t.forward(50)
    t.pop()
    t.pencolor(RED)
    t.circle(5)

    case("Recursive tree")
    t.reset()
    t.penup()
    t.goto(160, 200)
    t.setheading(90)
    t.pendown()
    _branch(60)

    case("Snowflake")
    t.reset()
    for _ in range(6):
        t.push()
        _snowflake_branch(70)
        t.pop()
        t.right(60)


# --- 11: Complex drawings ---------------------------------------------------
def _flower(x, y):
    t.penup()
    t.goto(x, y)
    t.pendown()
    t.pencolor(rgb(50, 150, 50))
    t.pensize(2)
    t.setheading(0)
    t.forward(30)
    t.fillcolor(rgb(255, 100, 150))
    for _ in range(6):
        t.push()
        t.fcircle(8)
        t.pop()
        t.right(60)
    t.fillcolor(YELLOW)
    t.fcircle(5)
    t.pensize(1)


def test_complex():
    section("Complex drawings")

    case("A house")
    t.reset()
    t.fillcolor(rgb(200, 150, 100))
    t.penup()
    t.goto(160, 150)
    t.rectangle(100, 80)
    t.goto(160, 110)
    t.pendown()
    t.fillcolor(rgb(150, 50, 50))
    t.begin_fill()
    t.goto(110, 110)
    t.goto(130, 80)
    t.goto(190, 80)
    t.goto(210, 110)
    t.goto(110, 110)
    t.end_fill()
    t.penup()
    t.goto(160, 170)
    t.fillcolor(rgb(100, 50, 0))
    t.rectangle(25, 40)
    t.goto(130, 135)
    t.fillcolor(rgb(200, 220, 255))
    t.rectangle(20, 20)

    case("Flower garden")
    t.reset()
    t.penup()
    t.goto(160, 180)
    t.fillcolor(BROWN)
    t.rectangle(320, 80)
    for x in range(70, 251, 60):
        _flower(x, 150 + int(rnd() * 10))

    case("Spiral galaxy")
    t.reset()
    t.pensize(2)
    for i in range(1, 101):
        t.pencolor(rgb(min(255, 100 + i), min(255, 100 + i), 255))
        t.forward(i * 0.8)
        t.right(20)

    case("Mandala")
    t.reset()
    for layer in range(1, 4):
        for i in range(1, 13):
            t.home()
            t.setheading(i * 30)
            t.forward(layer * 25)
            t.fillcolor(rgb(255 - layer * 60, 100 + layer * 40, 200))
            t.fcircle(20 - layer * 5)


# --- 12: Stress -------------------------------------------------------------
def test_stress():
    section("Stress")

    case("1000 random lines")
    t.reset()
    t0 = time.ticks_ms()
    for _ in range(1000):
        t.penup()
        t.goto(rnd() * 300 + 10, rnd() * 220 + 10)
        t.pendown()
        t.setheading(rnd() * 360)
        t.forward(10)
    print("    time:", time.ticks_diff(time.ticks_ms(), t0), "ms")

    case("100 random circles")
    t.reset()
    t0 = time.ticks_ms()
    for _ in range(100):
        t.penup()
        t.goto(rnd() * 280 + 20, rnd() * 200 + 20)
        t.fillcolor(rgb(int(rnd() * 255), int(rnd() * 255), int(rnd() * 255)))
        t.fcircle(int(rnd() * 15 + 5))
    print("    time:", time.ticks_diff(time.ticks_ms(), t0), "ms")

    case("100-sided polygon fill")
    t.reset()
    t.fillcolor(rgb(200, 100, 255))
    t0 = time.ticks_ms()
    t.begin_fill()
    for _ in range(100):
        t.forward(3)
        t.right(3.6)
    t.end_fill()
    print("    time:", time.ticks_diff(time.ticks_ms(), t0), "ms")


def run():
    global t
    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock to the new resolution before drawing
    t = Turtle()
    pcconsole.console("serial")  # keep the screen for the turtle
    print("Turtle test suite -- any key to advance, or auto after 1.5 s")
    try:
        test_basic_movement()
        test_pen_control()
        test_position()
        test_circles()
        test_rectangles()
        test_arcs()
        test_polygon_fill()
        test_stack()
        test_complex()
        test_stress()
        _hold()  # view the final drawing
        print()
        print("=== not ported: animated cursor, ARECT ===")
        print("ALL TESTS COMPLETE")
    finally:
        pcconsole.console("both")


run()
