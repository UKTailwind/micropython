import pcsprite as sp
import time

d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

SKY = d.colour(0x203050)
d.fill(SKY)

# draw a duck in the corner, grab it, erase the master
d.ellipse(10, 9, 9, 7, d.colour(YELLOW), True)       # body
d.ellipse(20, 5, 5, 4, d.colour(YELLOW), True)       # head
d.fill_rect(24, 4, 4, 2, d.colour(ORANGE))           # beak
duck = sp.grab(0, 0, 28, 18, transparent=SKY)
# erase the master
d.fill_rect(0, 0, 28, 18, SKY)

duck.show(50, 120)
# no cursor over the show
console("none")

try:
    while True:
        duck.x += 2
        if duck.x > W:
            duck.x = -28
        sp.update(vsync=True)
finally:
    sp.reset()
    # chapter 17's manners
    console()
