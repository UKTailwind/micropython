import time
from pcimage import load_image

# the maps below are sized for 640x480
screen(hdmi.RGB640)
time.sleep(3)

d = hdmi.fb()
sheet = load_image("tiles.bmp")

# exactly one 640x480 screen
tm = TileMap(sheet, 16, 16, cols=40, rows=30)
tm.fill(1)                                      # grass everywhere
# a tree at column 5, row 3
tm.set(5, 3, 3)
tm.set(6, 3, 3)                                 # and a neighbour
# flowers mid-meadow
tm.set(20, 12, 5)
# a river along row 20
for col in range(40):
    tm.set(col, 20, 2)

tm.view(0, 0)
tm.draw()
time.sleep(8)
