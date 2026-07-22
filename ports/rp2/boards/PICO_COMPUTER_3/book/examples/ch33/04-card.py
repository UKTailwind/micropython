# card.py -- an about-screen with a scannable QR of the project
# repo.
from uQR import QRCode
import os

REPO = "https://github.com/UKTailwind/micropython"

def card():
    d = hdmi.fb()
    d.fill(d.colour(WHITE))
    black = d.colour(BLACK)

    # the QR code, lower-right
    qr = QRCode(border=2)
    qr.add_data(REPO)
    grid = qr.get_matrix()
    n = len(grid)
    scale = 300 // n
    x0 = 640 - n * scale - 30
    y0 = 480 - n * scale - 30
    for r in range(n):
        for c in range(n):
            if grid[r][c]:
                d.fill_rect(x0 + c * scale, y0 + r * scale, scale,
                            scale, black)

    # the words, upper-left
    hdmi.text("PICO COMPUTER 3", 30, 50, black, -1, 4)      # big
    # full name + version
    hdmi.text(os.uname().machine, 30, 110, black, -1, 1)
    hdmi.text("Scan to build your own", 30, 430, black, -1, 2)

card()
