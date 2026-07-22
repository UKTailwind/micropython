d = hdmi.fb()
MASK = d.colour(MAGENTA)
d.fill(MASK)                                   # transparent-to-be

# four 32x32 cells in a row
for i in range(4):
    x = i * 32
    d.fill_rect(x + 4, 10, 24, 18, d.colour(GOLD))       # body
    d.fill_rect(x + 8, 4, 16, 8, d.colour(LITEGRAY))     # head
    # left leg
    d.fill_rect(x + 6, 28, 4, 4 - i % 2 * 2, d.colour(RED))
    # right leg
    d.fill_rect(x + 22, 28, 4, 2 + i % 2 * 2, d.colour(RED))

save_image("robot.bmp")
