d = hdmi.fb()
d.fill(d.colour(BLACK))

GRASS = d.colour(0x1E7A1E)
DARK = d.colour(0x156015)

# tile 1: grass
d.fill_rect(0, 0, 16, 16, GRASS)
for i in range(6):
    d.pixel(2 + i * 2, (i * 5) % 16, DARK)

# tile 2: water
d.fill_rect(16, 0, 16, 16, d.colour(0x1040A0))
d.hline(18, 5, 10, d.colour(0x3070D0))
d.hline(20, 11, 10, d.colour(0x3070D0))

# tile 3: tree
d.fill_rect(32, 0, 16, 16, GRASS)
d.fill_rect(39, 10, 3, 5, d.colour(BROWN))
d.ellipse(40, 6, 6, 6, d.colour(MYRTLE), True)

# tile 4: rock
d.fill_rect(48, 0, 16, 16, GRASS)
d.rbox(50, 4, 12, 10, 3, d.colour(GRAY), d.colour(GRAY))

# tile 5: flowers
d.fill_rect(64, 0, 16, 16, GRASS)
for fx, fy in ((68, 4), (74, 9), (69, 12)):
    d.pixel(fx, fy - 1, d.colour(MAGENTA))
    d.pixel(fx - 1, fy, d.colour(MAGENTA))
    d.pixel(fx + 1, fy, d.colour(MAGENTA))
    d.pixel(fx, fy + 1, d.colour(MAGENTA))
    d.pixel(fx, fy, d.colour(YELLOW))

# tile 6: the hero (on black = cut-out)
d.fill_rect(84, 6, 8, 7, d.colour(GOLD))
d.fill_rect(85, 1, 6, 5, d.colour(LITEGRAY))
d.fill_rect(84, 13, 3, 3, d.colour(RED))
d.fill_rect(89, 13, 3, 3, d.colour(RED))

# tile 7: the elder (blue robe, white beard)
d.fill_rect(100, 5, 8, 9, d.colour(COBALT))
d.fill_rect(101, 1, 6, 5, d.colour(LITEGRAY))
d.fill_rect(102, 5, 4, 3, d.colour(WHITE))
d.fill_rect(100, 14, 8, 2, d.colour(GRAY))

# tile 8: an amulet shard (on grass, so it sits in the world)
d.fill_rect(112, 0, 16, 16, GRASS)
d.fill_rect(117, 4, 6, 6, d.colour(GOLD))
d.fill_rect(119, 2, 2, 2, d.colour(GOLD))
d.fill_rect(118, 10, 4, 3, d.colour(YELLOW))

save_image("tiles2.bmp")
print("tileset saved: 8 tiles")
