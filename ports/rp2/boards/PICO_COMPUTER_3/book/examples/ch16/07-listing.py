import time

d = hdmi.fb()
MASK = d.colour(MAGENTA)
bot = load_image("robot.bmp", transparent=MASK)

d.fill(d.colour(0x102040))
for step in range(60):
    frame = step % 4
    bot.cell(frame, 0, 32, 32, 40 + step * 8, 200, skip=MASK)
    time.sleep(0.1)
