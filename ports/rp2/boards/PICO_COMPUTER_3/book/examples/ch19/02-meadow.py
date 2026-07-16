import time
from pcimage import load_image

screen(hdmi.RGB640)                 # the maps below are sized for 640x480
time.sleep(3)

d = hdmi.fb()
sheet = load_image("tiles.bmp")

tm = TileMap(sheet, 16, 16, cols=40, rows=30)   # exactly one 640x480 screen
tm.fill(1)                                      # grass everywhere
tm.set(5, 3, 3)                                 # a tree at column 5, row 3
tm.set(6, 3, 3)                                 # and a neighbour
tm.set(20, 12, 5)                               # flowers mid-meadow
for col in range(40):                           # a river along row 20
    tm.set(col, 20, 2)

tm.view(0, 0)
tm.draw()
time.sleep(8)
