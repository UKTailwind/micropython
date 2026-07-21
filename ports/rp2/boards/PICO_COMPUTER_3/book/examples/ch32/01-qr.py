# qr.py -- draw any text as a scannable QR code on the screen.
from uQR import QRCode

def show_qr(text):
    qr = QRCode(border=2)          # a 2-cell quiet zone (the white margin)
    qr.add_data(text)
    grid = qr.get_matrix()         # a square grid of True/False; True = a black cell
    n = len(grid)

    d = hdmi.fb()
    d.fill(d.colour(WHITE))        # a QR MUST sit on light -- readers need the contrast
    black = d.colour(BLACK)

    scale = 440 // n               # fill most of the 480-tall screen
    x0 = (640 - n * scale) // 2    # ...centred
    y0 = (480 - n * scale) // 2
    for row in range(n):
        for col in range(n):
            if grid[row][col]:
                d.fill_rect(x0 + col * scale, y0 + row * scale, scale, scale, black)

if __name__ == "__main__":
    show_qr("https://github.com/UKTailwind/micropython")
