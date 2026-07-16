hdmi.close("F")                   # clear away any previous workbench
hdmi.create()                     # the invisible workbench
hdmi.write("F")                   # all drawing now lands off-screen
d = hdmi.fb()                     # a Display over F -- AFTER write()!
W = hdmi.width()
H = hdmi.height()

x, y = 100, 100
dx, dy = 5, 3

console("none")                   # and no console cursor over the show

try:
    while True:
        d.fill(d.colour(0x102040))                      # fresh canvas
        x += dx
        y += dy
        if x < 12 or x > W - 12:
            dx = -dx
        if y < 12 or y > H - 12:
            dy = -dy
        d.ellipse(x, y, 10, 10, d.colour(GOLD), True)   # scene, unseen
        hdmi.vsync()
        hdmi.copy("F", "N")                             # one clean reveal
finally:
    hdmi.write("N")               # ALWAYS hand the screen back...
    console()                     # ...and the console with it
