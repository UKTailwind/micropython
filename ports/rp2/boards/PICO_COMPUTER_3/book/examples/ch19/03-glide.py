import random
import time
from pcimage import load_image

screen(hdmi.RGB640)
time.sleep(3)

sheet = load_image("tiles.bmp")
tm = TileMap(sheet, 16, 16, cols=80, rows=60)     # 1280x960 world
tm.fill(1)
random.seed(7)
for _ in range(120):                              # scatter scenery
    tm.set(random.randint(0, 79), random.randint(0, 59),
           random.randint(2, 5))

tm.clamp(hdmi.width(), hdmi.height())             # camera stays on the map

hdmi.close("F")
hdmi.create()
console("none")

try:
    vx = 0
    for vy in range(0, 960 - 480 + 1, 2):         # a slow southward drift
        vx = min(vx + 1, 1280 - 640)
        tm.view(vx, vy)
        hdmi.write("F")
        tm.draw()
        hdmi.write("N")
        hdmi.vsync()
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
