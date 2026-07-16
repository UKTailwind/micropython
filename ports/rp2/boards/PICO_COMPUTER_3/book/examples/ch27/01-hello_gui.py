import time
import pcgui

screen(hdmi.RGB320)                 # chunky pixels suit fingers
time.sleep(3)
console("serial")                   # REPL prints stay off the GUI screen

hdmi.fb().fill(0)
g = pcgui.GUI()                     # the manager: owns, draws, dispatches
g.start()                           # borrow the keyboard (for text boxes)
done = [False]

g.caption(100, 10, "FIRST CONTACT", fg=YELLOW, font=2)
lamp = g.led(60, 80, 10, "status", GREEN)

def flip(sw):
    lamp.value = sw.value

def quit_app(b):
    done[0] = True

g.switch(120, 66, 90, 28, "ON|OFF", callback=flip)
g.button(110, 180, 100, 30, "QUIT", fg=WHITE, bg=RED, callback=quit_app)

try:
    while not done[0]:
        g.poll()                    # everything happens in here
        time.sleep_ms(10)
finally:
    g.stop()                        # give the keyboard back
    console()
