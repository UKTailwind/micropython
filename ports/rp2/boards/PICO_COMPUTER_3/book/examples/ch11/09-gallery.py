import shapes

t = Turtle()
t.reset()

t.pencolor(YELLOW)
shapes.star(t, 7, 90)

t.penup(); t.goto(120, 120); t.pendown()
t.pencolor(CYAN)
shapes.flower(t, 9, 40)

t.penup(); t.goto(520, 360); t.pendown()
t.pencolor(RED)
shapes.burst(t, 24, 60)
