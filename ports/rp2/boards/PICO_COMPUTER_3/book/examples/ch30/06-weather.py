import time
import keyboard
import requests

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

# London -- point it at YOUR sky
LAT, LON = 51.5, -0.13
URL = ("https://api.open-meteo.com/v1/forecast"
       "?latitude=" + str(LAT) +
       "&longitude=" + str(LON) + "&current_weather=true")
REFRESH_MS = 10 * 60 * 1000

SKIES = {0: "clear sky", 1: "mostly clear", 2: "partly cloudy",
         3: "overcast", 45: "fog", 51: "drizzle", 61: "light "
             "rain",
         63: "rain", 65: "heavy rain", 71: "snow", 80: "showers",
         95: "thunderstorm"}

def fetch():
    """The current weather dict, or None. Never raises, always
    closes."""
    try:
        r = requests.get(URL)
    except OSError:
        return None
    try:
        if r.status_code != 200:
            return None
        return r.json()["current_weather"]
    except (OSError, ValueError, KeyError):
        return None
    finally:
        r.close()

screen(hdmi.RGB640)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()
d = hdmi.fb()

BG = d.colour(0x0A1420)
INK = d.colour(WHITE)
BIG = d.colour(GOLD)
DIM = d.colour(GRAY)

wx = None                          # the last GOOD reading
stamp = ""                         # when we got it
failures = 0

def redraw():
    d.fill(BG)
    hdmi.text(f"WEATHER  {LAT}, {LON}", 16, 12, DIM)
    if wx is None:
        hdmi.text("asking the sky...", 200, 220, INK, -1, 1, 3)
    else:
        t = f"{wx['temperature']:.0f}"
        # 72x96 digits
        x = hdmi.text(t, 60, 120, BIG, -1, 3, 5)
        # the degree ring
        d.ellipse(x + 18, 132, 10, 10, BIG)
        sky = SKIES.get(wx["weathercode"], "sky doing something")
        hdmi.text(sky, 60, 260, INK, -1, 1, 3)
        hdmi.text(f"wind {wx['windspeed']:.0f} km/h", 60, 300,
                  INK)
        status = "updated " + stamp
        if failures:
            status += (f"  --  offline, showing last good "
                       f"(x{failures})")
        hdmi.text(status, 16, H - 24, DIM)
    hdmi.text("R refresh   ESC quit", W - 176, 12, DIM)

try:
    wifi()                         # saved credentials, if any
# none saved? the fetch loop copes
except Exception:
    pass
console("none")

last_try = None
r_was = True

try:
    redraw()
    while not held(keyboard.ESC):
        now = time.ticks_ms()
        r_now = held(ord("r"))
        force = r_now and not r_was
        r_was = r_now
        if force or last_try is None or \
                time.ticks_diff(now, last_try) >= REFRESH_MS:
            last_try = now
            fresh = fetch()
            if fresh is not None:
                wx = fresh
                stamp = "{:02}:{:02}".format(*gettime()[3:5])
                failures = 0
            else:
                failures += 1
            redraw()
        time.sleep_ms(100)
finally:
    console()
