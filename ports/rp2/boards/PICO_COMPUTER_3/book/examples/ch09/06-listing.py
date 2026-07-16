t = Turtle()
t.reset()
t.pencolor(CYAN)

step = 2
for _ in range(80):
    t.forward(step)
    t.right(90)
    step += 3
