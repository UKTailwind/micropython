import asyncio
import time
import keyboard
import requests

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

URL = ("https://api.open-meteo.com/v1/forecast?latitude=51.5"
       "&longitude=-0.13&current_weather=true")

screen(hdmi.RGB640)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()

hdmi.close("F")
hdmi.create()
hdmi.write("F")
d = hdmi.fb()

BG = d.colour(0x080814)
INK = d.colour(GOLD)
DIM = d.colour(GRAY)

# shared freely: asyncio's gift
report = ["fetching..."]

async def fetcher():
    while True:
        try:
            # blocking! see the prose
            r = requests.get(URL)
            try:
                if r.status_code == 200:
                    wx = r.json()["current_weather"]
                    report[0] = (f"{wx['temperature']:.0f} "
                                  f"C outside")
                else:
                    report[0] = "sky unavailable"
            finally:
                r.close()
        except (OSError, ValueError, KeyError):
            report[0] = "offline"
        await asyncio.sleep(60)

async def bouncer():
    x, y = 100.0, 100.0
    dx, dy = 220.0, 150.0
    last = time.ticks_ms()
    while True:
        now = time.ticks_ms()
        dt = time.ticks_diff(now, last) / 1000
        last = now
        x += dx * dt
        y += dy * dt
        if x < 12 or x > W - 12:
            x = max(12, min(W - 12, x))
            dx = -dx
        if y < 12 or y > H - 12:
            y = max(12, min(H - 12, y))
            dy = -dy
        d.fill(BG)
        d.ellipse(int(x), int(y), 10, 10, INK, True)
        hdmi.text(report[0], 16, 12, DIM)
        hdmi.vsync()
        hdmi.copy("F", "N")
        await asyncio.sleep_ms(5)    # the volunteer's pause

async def main():
    asyncio.create_task(fetcher())
    b = asyncio.create_task(bouncer())
    while not held(keyboard.ESC):
        await asyncio.sleep_ms(50)
    b.cancel()

console("none")
try:
    wifi()
except Exception:
    pass
try:
    asyncio.run(main())
finally:
    hdmi.write("N")
    console()
