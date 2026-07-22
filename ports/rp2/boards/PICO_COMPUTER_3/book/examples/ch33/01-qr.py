# qr.py -- draw any text as a scannable QR code on the screen.
from uQR import QRCode

def show_qr(text):
    # a 2-cell quiet zone (the white margin)
    qr = QRCode(border=2)
    qr.add_data(text)
    # a square grid of True/False; True = a black cell
    grid = qr.get_matrix()
    n = len(grid)

    d = hdmi.fb()
    # a QR MUST sit on light -- readers need the contrast
    d.fill(d.colour(WHITE))
    black = d.colour(BLACK)

    # fill most of the 480-tall screen
    scale = 440 // n
    x0 = (640 - n * scale) // 2    # ...centred
    y0 = (480 - n * scale) // 2
    for row in range(n):
        for col in range(n):
            if grid[row][col]:
                d.fill_rect(x0 + col * scale, y0 + row * scale,
                            scale, scale, black)

if __name__ == "__main__":
    show_qr("https://github.com/UKTailwind/micropython")
