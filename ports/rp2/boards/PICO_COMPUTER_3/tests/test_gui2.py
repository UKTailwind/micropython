# pcgui phase-2 controls demo/interactive test: display box, spinner, list box,
# format box and area. Needs a USB mouse or touch panel (and a keyboard only if
# you tap the format box to edit it). Visual/interactive; not part of test_all.
#
#   run("/sd/tests/test_gui2.py")

import time

import hdmi
import keyboard
import pcgui
from pcgfx import WHITE, YELLOW, CYAN, GREEN, RED


def run():
    import pcconsole

    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock
    pcconsole.console("serial")
    hdmi.fill(0)

    g = pcgui.GUI()
    g.start()
    quit_flag = [False]

    g.caption(120, 4, "GUI Demo 2", fg=YELLOW, font=2)

    # A spinner whose value is echoed into a read-only display box.
    g.caption(10, 26, "Level", fg=CYAN, font=1)
    status = g.displaybox(10, 66, 140, 20, text="Level 50", font=1)

    def on_spin(sp):
        status.value = "Level %d" % sp.value

    g.spinner(10, 40, 140, 24, value=50, lo=0, hi=100, step=5, font=2, callback=on_spin)

    # A list box whose selection is echoed into the same display box.
    def on_list(lb):
        status.value = "Pick: " + lb.text

    g.listbox(10, 92, 140, 108,
              ["Economy", "Normal", "High", "Turbo", "Custom", "Manual", "Auto"],
              selected=1, font=1, callback=on_list)

    # A format box (tap to edit; shows a currency format).
    g.caption(160, 26, "Price", fg=CYAN, font=1)
    g.fmtbox(160, 40, 140, 24, value=19.99, fmt="$%.2f", font=2)

    # An area used as a little scribble canvas (a frame marks its bounds).
    g.frame(160, 92, 150, 92, "Scribble", font=1)
    dot = g.c(YELLOW)

    def on_area(a):
        ax, ay = a.value
        g.d.ellipse(a.x + ax, a.y + ay, 2, 2, dot, True)

    g.area(163, 104, 144, 78, callback=on_area)

    def on_quit(b):
        quit_flag[0] = True

    g.button(220, 208, 90, 26, "QUIT", fg=YELLOW, bg=RED, font=2, callback=on_quit)
    g.caption(6, 216, "spin / pick / edit price / scribble", fg=WHITE, font=7)

    print("GUI demo 2 running -- QUIT (or a key) to exit")
    try:
        while not quit_flag[0]:
            g.poll()
            if g.focus is None and keyboard.keydown(0):
                break
            time.sleep_ms(15)
    finally:
        g.stop()
        pcconsole.console("both")
        import testutil as T

        T.restore_screen()
    print("GUI demo 2 finished")


if __name__ == "__main__":
    run()
