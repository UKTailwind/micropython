t = Turtle()
t.reset()
t.pencolor(MAGENTA)

for petal in range(12):        # twelve petals...
    for _ in range(4):         # ...each petal is a small square
        t.forward(70)
        t.right(90)
    t.right(30)                # rotate a twelfth of a turn, next petal
