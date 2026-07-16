def polygon(t, sides, size=60):
    for _ in range(sides):
        t.forward(size)
        t.right(360 / sides)

def flower(t, petals, size):
    for _ in range(petals):
        polygon(t, 4, size)
        t.right(360 / petals)

t = Turtle()
t.reset()
t.pencolor(MAGENTA)
flower(t, 12, 70)
