import time

d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

d.fill(d.colour(0x102040))

x, y = 100, 100
dx, dy = 5, 3

while True:
    # NEW: erase old
    d.ellipse(x, y, 10, 10, d.colour(0x102040), True)
    x += dx
    y += dy
    if x < 12 or x > W - 12:
        dx = -dx
    if y < 12 or y > H - 12:
        dy = -dy
    d.ellipse(x, y, 10, 10, d.colour(GOLD), True)
    time.sleep(0.02)
