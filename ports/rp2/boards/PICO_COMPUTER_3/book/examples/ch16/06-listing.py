d = hdmi.fb()
MASK = d.colour(MAGENTA)
d.fill(MASK)                                   # transparent-to-be

for i in range(4):                             # four 32x32 cells in a row
    x = i * 32
    d.fill_rect(x + 4, 10, 24, 18, d.colour(GOLD))       # body
    d.fill_rect(x + 8, 4, 16, 8, d.colour(LITEGRAY))     # head
    d.fill_rect(x + 6, 28, 4, 4 - i % 2 * 2, d.colour(RED))   # left leg
    d.fill_rect(x + 22, 28, 4, 2 + i % 2 * 2, d.colour(RED))  # right leg

save_image("robot.bmp")
