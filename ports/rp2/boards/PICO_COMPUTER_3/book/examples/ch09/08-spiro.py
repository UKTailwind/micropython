t = Turtle()
t.reset()

angle = float(input("Angle (89, 121, 144 and 160 are lovely): "))
steps = int(input("Steps (try 150): "))

t.pencolor(CYAN)
d = 4
for i in range(steps):
    if i == steps // 3:
        t.pencolor(MAGENTA)
    elif i == 2 * steps // 3:
        t.pencolor(YELLOW)
    t.forward(d)
    t.right(angle)
    d += 1

t.penup()
t.goto(10, 10)
