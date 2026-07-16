d = hdmi.fb()
sheet = load_image("/sd/invaders.png", transparent=d.colour(MAGENTA))

sheet.cell(0, 0, 16, 16, 100, 80, skip=d.colour(MAGENTA))   # col 0, row 0
sheet.cell(3, 1, 16, 16, 200, 80, skip=d.colour(MAGENTA))   # col 3, row 1
