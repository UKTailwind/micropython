import time
import keyboard

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

screen(hdmi.RGB640)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()
d = hdmi.fb()

BG = d.colour(0x101828)
RING = d.colour(GOLD)
DIGITS = d.colour(WHITE)
DIM = d.colour(GRAY)
ALERT = d.colour(RED)

ALARM_FILE = "/alarm.txt"

def load_alarm():
    try:
        with open(ALARM_FILE) as f:
            hh, mm, armed = f.read().strip().split(",")
            return int(hh), int(mm), int(armed)
    except (OSError, ValueError):
        return 7, 0, 0

def save_alarm():
    with open(ALARM_FILE, "w") as f:
        f.write(f"{ahh},{amm},{armed}\n")

ahh, amm, armed = load_alarm()
ringing = False
fired_at = None                      # (hh, mm) already rung this minute
up_was = dn_was = a_was = True

d.fill(BG)
cx, cy = W // 2, H // 2
tw = 8 * 32                          # font 6: "HH:MM:SS"
x0, y0 = cx - tw // 2, cy - 25

console("none")

try:
    while not held(keyboard.ESC):
        up_now, dn_now = held(keyboard.UP), held(keyboard.DOWN)
        a_now = held(ord("a"))
        if up_now and not up_was:
            amm += 5
            ahh, amm = (ahh + amm // 60) % 24, amm % 60
            fired_at = None
            save_alarm()
        if dn_now and not dn_was:
            amm -= 5
            if amm < 0:
                amm += 60
                ahh = (ahh - 1) % 24
            fired_at = None
            save_alarm()
        if a_now and not a_was:
            armed = 0 if armed else 1
            fired_at = None
            save_alarm()
        up_was, dn_was, a_was = up_now, dn_now, a_now

        h, m, s = gettime()[3:6]

        if armed and (h, m) == (ahh, amm) and fired_at != (h, m):
            ringing = True
            fired_at = (h, m)
        if ringing:
            if keydown(0):           # any key silences
                ringing = False
                d.fill(BG)
            else:
                d.fill(ALERT if s % 2 else BG)
                tone(880, 880, 120, wait=True)
                tone(1318, 1318, 120, wait=True)

        d.fill_rect(x0, y0, tw, 50, BG)
        hdmi.text(f"{h:02}:{m:02}:{s:02}", x0, y0, DIGITS, -1, 1, 6)
        d.arc(cx, cy, 150, 158, 0, 0, BG)
        if s:
            d.arc(cx, cy, 150, 158, 0, s * 6, RING)
        status = f"alarm {ahh:02}:{amm:02}  " + ("ARMED" if armed else "off")
        hdmi.text(status, cx - len(status) * 8, H - 60,
                  RING if armed else DIM, BG, 2)
        hdmi.text("UP/DOWN set   A arm   ESC quit", cx - 116, H - 24, DIM, BG)
        time.sleep_ms(50)
finally:
    console()
