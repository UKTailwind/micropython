hdmi.close("F")                    # start clean (rerun-proof)
hdmi.create()
hdmi.write("F")                    # target first...
d = hdmi.fb()                      # ...then the Display over it
W = hdmi.width()
H = hdmi.height()

COLOURS = [GOLD, CYAN, MAGENTA, GREEN, ORANGE, LILAC]
LOGO = "PICO"
LW = len(LOGO) * 16 * 2            # font 3 at scale 2
LH = 24 * 2

x, y = 50, 80
dx, dy = 4, 3
ci = 0
bounces = 0

console("none")

try:
    while True:
        d.fill(d.colour(0x080810))
        x += dx
        y += dy
        hit = False
        if x < 0 or x + LW > W:
            # step back inside the edge
            x = max(0, min(x, W - LW))
            dx = -dx
            hit = True
        if y < 0 or y + LH > H:
            y = max(0, min(y, H - LH))
            dy = -dy
            hit = True
        if hit:
            bounces += 1
            ci = (ci + 1) % len(COLOURS)
        hdmi.text(LOGO, x, y, d.colour(COLOURS[ci]), -1, 2, 3)
        hdmi.text(f"bounces: {bounces}", 8, H - 20,
                  d.colour(GRAY))
        hdmi.vsync()
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
