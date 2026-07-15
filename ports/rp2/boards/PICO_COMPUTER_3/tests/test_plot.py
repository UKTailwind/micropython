# plot() + pcgame.Clock demo: a few static plots, then an animated scrolling
# sine at a fixed frame rate (shows the drift-free Clock and its measured fps).
# Visual demo; press a key to advance / stop.
#
#   run("/sd/tests/test_plot.py")

import math
import time

import hdmi
import keyboard
import pcgame
import pcplot


def _wait(timeout=4000):
    t0 = time.ticks_ms()
    while keyboard.keydown(0) == 0:
        if time.ticks_diff(time.ticks_ms(), t0) > timeout:
            return
        time.sleep_ms(20)
    while keyboard.keydown(0):
        time.sleep_ms(20)


def run():
    import pcconsole

    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock
    pcconsole.console("serial")
    d = hdmi.fb()
    white = d.colour(0xFFFFFF)

    def title(s):
        d.text(s, 4, 0, white, font=1)

    # 1. a list -> line plot
    pcplot.plot([1, 4, 9, 16, 25, 36, 49, 64])
    title("plot(list) - squares")
    _wait()

    # 2. a function over a range
    pcplot.plot(lambda t: math.sin(t) * math.exp(-t / 6), (0, 6 * math.pi))
    title("plot(function) - damped sine")
    _wait()

    # 3. a bar chart
    pcplot.plot([3, 7, 2, 8, 5, 9, 4, 6], style="bar", colour=0x00FF00)
    title("plot(style=bar)")
    _wait()

    # 4. several series
    xs = [i / 10 for i in range(63)]
    pcplot.plot([[math.sin(t) for t in xs], [math.cos(t) for t in xs]])
    title("plot(multiple series)")
    _wait()

    # 5. animated scrolling sine at a fixed 30 fps (drift-free Clock)
    clock = pcgame.Clock(30)
    phase = 0.0
    print("animating -- press a key to stop")
    while keyboard.keydown(0) == 0:
        dt = clock.tick()
        phase += dt * 3
        ys = [math.sin(x / 8 + phase) for x in range(0, 320, 4)]
        pcplot.plot(ys, colour=0x00FFFF, axes=False)
        d.text("fps %.0f" % clock.fps, 4, 0, white, font=1)
    while keyboard.keydown(0):
        time.sleep_ms(20)

    pcconsole.console("both")
    import testutil as T

    T.restore_screen()
    print("plot demo finished")


if __name__ == "__main__":
    run()
