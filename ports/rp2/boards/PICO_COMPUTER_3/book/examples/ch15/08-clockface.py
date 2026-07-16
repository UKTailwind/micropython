import time

d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

BG = d.colour(0x101828)
RING = d.colour(GOLD)
DIGITS = d.colour(WHITE)

d.fill(BG)
cx = W // 2
cy = H // 2

# font 6 is 32x50: "HH:MM:SS" is 8 characters
tw = 8 * 32
x0 = cx - tw // 2
y0 = cy - 25

while True:
    h, m, s = gettime()[3:6]
    d.fill_rect(x0, y0, tw, 50, BG)                # erase old digits
    hdmi.text(f"{h:02}:{m:02}:{s:02}", x0, y0, DIGITS, -1, 1, 6)

    d.arc(cx, cy, 150, 158, 0, 0, BG)              # erase the ring
    if s:
        d.arc(cx, cy, 150, 158, 0, s * 6, RING)    # sweep: 6 deg per second

    time.sleep(1)
