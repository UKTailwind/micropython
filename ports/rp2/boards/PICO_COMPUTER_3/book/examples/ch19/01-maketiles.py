d = hdmi.fb()
d.fill(d.colour(BLACK))

GRASS = d.colour(0x1E7A1E)
DARK = d.colour(0x156015)

# tile 1: grass (x 0-15), speckled
d.fill_rect(0, 0, 16, 16, GRASS)
for i in range(6):
    d.pixel(2 + i * 2, (i * 5) % 16, DARK)

# tile 2: water
d.fill_rect(16, 0, 16, 16, d.colour(0x1040A0))
d.hline(18, 5, 10, d.colour(0x3070D0))
d.hline(20, 11, 10, d.colour(0x3070D0))

# tile 3: tree (on grass)
d.fill_rect(32, 0, 16, 16, GRASS)
d.fill_rect(39, 10, 3, 5, d.colour(BROWN))
d.ellipse(40, 6, 6, 6, d.colour(MYRTLE), True)

# tile 4: rock (on grass)
d.fill_rect(48, 0, 16, 16, GRASS)
d.rbox(50, 4, 12, 10, 3, d.colour(GRAY), d.colour(GRAY))

# tile 5: flowers (on grass)
d.fill_rect(64, 0, 16, 16, GRASS)
for fx, fy in ((68, 4), (74, 9), (69, 12)):
    d.pixel(fx, fy - 1, d.colour(MAGENTA))
    d.pixel(fx - 1, fy, d.colour(MAGENTA))
    d.pixel(fx + 1, fy, d.colour(MAGENTA))
    d.pixel(fx, fy + 1, d.colour(MAGENTA))
    d.pixel(fx, fy, d.colour(YELLOW))

# tile 6: the explorer (on black -- black becomes the cut-out)
d.fill_rect(84, 6, 8, 7, d.colour(GOLD))          # body
d.fill_rect(85, 1, 6, 5, d.colour(LITEGRAY))      # head
d.fill_rect(84, 13, 3, 3, d.colour(RED))          # boots
d.fill_rect(89, 13, 3, 3, d.colour(RED))

save_image("tiles.bmp")
print("tileset saved")
