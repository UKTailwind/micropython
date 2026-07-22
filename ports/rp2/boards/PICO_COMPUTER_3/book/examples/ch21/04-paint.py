import time
import keyboard
import pcsprite as sp

screen(hdmi.RGB320)
# let the monitor lock the mode
time.sleep(3)

d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

PALETTE = [WHITE, RED, ORANGE, YELLOW, GREEN, CYAN, COBALT,
           MAGENTA]
BG = d.colour(0x101010)
d.fill(BG)
hdmi.text("1-8 colour  [ ] size  c clear  s save  Esc quit",
          4, H - 12, d.colour(GRAY))

# crosshair cursor: draw, grab, wipe (chapter 18's move)
d.line(8, 0, 8, 16, d.colour(WHITE), 1)
d.line(0, 8, 16, 8, d.colour(WHITE), 1)
cursor = sp.grab(0, 0, 17, 17, transparent=BG)
d.fill_rect(0, 0, 17, 17, BG)
cursor.show(W // 2, H // 2)

colour = d.colour(WHITE)
size = 3

console("none")

try:
    while True:
        k = keydown(1)
        if k == keyboard.ESC:
            break
        if ord("1") <= k <= ord("8"):
            colour = d.colour(PALETTE[k - ord("1")])
        elif k == ord("["):
            size = max(1, size - 1)
        elif k == ord("]"):
            size = min(20, size + 1)
        elif k == ord("c"):
            d.fill(BG)
        elif k == ord("s"):
            save_image("painting.bmp")

        mx = mouse("X")
        my = mouse("Y")
        # centre the crosshair
        cursor.x = mx - 8
        cursor.y = my - 8
        if mouse("L"):
            d.ellipse(mx, my, size, size, colour, True)

        tx = touch("X")                      # fingers paint too
        if touch("DOWN") and tx >= 0:
            d.ellipse(tx, touch("Y"), size, size, colour, True)

        sp.update(vsync=True)
finally:
    sp.reset()
    console()
    # back to the roomy default
    screen(hdmi.RGB640)
