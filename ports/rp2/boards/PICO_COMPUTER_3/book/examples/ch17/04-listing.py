# clear away any previous workbench
hdmi.close("F")
hdmi.create()                     # the invisible workbench
# all drawing now lands off-screen
hdmi.write("F")
# a Display over F -- AFTER write()!
d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

x, y = 100, 100
dx, dy = 5, 3

# and no console cursor over the show
console("none")

try:
    while True:
        # fresh canvas
        d.fill(d.colour(0x102040))
        x += dx
        y += dy
        if x < 12 or x > W - 12:
            dx = -dx
        if y < 12 or y > H - 12:
            dy = -dy
        # scene, unseen
        d.ellipse(x, y, 10, 10, d.colour(GOLD), True)
        hdmi.vsync()
        # one clean reveal
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")               # ALWAYS hand the screen back...
    console()                     # ...and the console with it
