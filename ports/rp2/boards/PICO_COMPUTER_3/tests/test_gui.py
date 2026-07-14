# pcgui demo/interactive test -- a small control panel on the HDMI screen.
#
# Needs a USB mouse (or touch panel) to click, and a USB keyboard to type into
# the text boxes. Drive the switch, slider, radios, check box and buttons and
# watch the LED / gauges respond; press QUIT (or any key with no box focused)
# to leave. This is a visual/interactive test, not part of test_all.py.
#
#   run("/sd/tests/test_gui.py")

import time

import hdmi
import keyboard
import pcgui
from pcgfx import WHITE, GREEN, RED, YELLOW, CYAN


def run():
    import pcconsole

    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock before drawing
    pcconsole.console("serial")
    hdmi.fill(0)

    g = pcgui.GUI()
    g.start()

    quit_flag = [False]

    # Heading
    g.caption(120, 4, "GUI Demo", fg=YELLOW, font=2)

    # Pump frame: a switch drives the "Run" LED.
    g.frame(8, 26, 150, 74, "Pump", font=2)
    led = g.led(18, 48, 8, "Run", GREEN)


    def on_switch(sw):
        led.value = sw.value

    g.switch(18, 66, 80, 26, "ON|OFF", value=0, font=2, callback=on_switch)

    # A slider driving a circular gauge and a bar gauge.
    g.caption(170, 26, "Level", fg=CYAN, font=2)
    gauge = g.gauge(210, 78, 34, value=0, lo=0, hi=100, fg=CYAN, font=2)
    bar = g.bargauge(255, 44, 20, 70, value=0, lo=0, hi=100, fg=GREEN)

    def on_slider(sl):
        gauge.value = sl.value
        bar.value = sl.value

    g.slider(168, 115, 130, 18, value=0, lo=0, hi=100, callback=on_slider)

    # Radios (mode) + a check box.
    g.frame(8, 108, 150, 92, "Mode", font=2)
    g.radio(20, 126, 7, "Eco", group=1, value=1, font=1)
    g.radio(20, 148, 7, "Normal", group=1, font=1)
    g.radio(20, 170, 7, "High", group=1, font=1)
    g.checkbox(90, 126, 16, "Log", value=1, font=1)

    # Text + number entry.
    g.caption(168, 138, "Name", fg=WHITE, font=1)
    g.textbox(168, 152, 130, 20, text="PUMP1", font=1)
    g.caption(168, 178, "Set", fg=WHITE, font=1)
    g.numberbox(200, 176, 98, 20, value="20.5", font=1)

    # Quit button.
    def on_quit(b):
        quit_flag[0] = True

    g.button(8, 208, 90, 26, "QUIT", fg=YELLOW, bg=RED, font=2, callback=on_quit)
    g.caption(110, 216, "click a control; type in the boxes", fg=WHITE, font=7)

    print("GUI demo running -- click QUIT (or press a key) to exit")
    try:

        while not quit_flag[0]:
            g.poll()
            # a bare keypress with nothing focused also quits
            if g.focus is None and keyboard.keydown(0):
                break
            time.sleep_ms(15)
    finally:
        g.stop()
        pcconsole.console("both")
        import testutil as T

        T.restore_screen()
    print("GUI demo finished")


if __name__ == "__main__":
    run()