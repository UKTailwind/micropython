import time
import keyboard
import pccursor

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

screen(hdmi.RGB640)
time.sleep(3)

d = hdmi.fb()
d.fill(d.colour(0x103028))
hdmi.text("click to stamp -- ESC quits", 8, 8, d.colour(GRAY))

console("none")
pccursor.on(pccursor.CROSS, colour=CYAN)

click_was = True
try:
    while not held(keyboard.ESC):
        pccursor.refresh()
        click_now = mouse("L") == 1
        if click_now and not click_was:          # just-pressed (ch 25)
            pccursor.erase()                     # lift it -- no ghosts
            d.ellipse(mouse("X"), mouse("Y"), 6, 6, d.colour(GOLD), True)
        click_was = click_now
        time.sleep_ms(10)
finally:
    pccursor.off()
    console()
