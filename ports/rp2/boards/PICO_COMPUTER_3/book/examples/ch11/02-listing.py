def square(t, size):
    for _ in range(4):
        t.forward(size)
        t.right(90)

t = Turtle()
t.reset()
square(t, 40)
square(t, 80)
square(t, 160)
