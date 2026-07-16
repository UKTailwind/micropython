angle = float(input("Angle? (try 100, 135, 144, 160, 170) "))

t = Turtle()
t.reset()
for _ in range(36):
    t.forward(120)
    t.right(angle)
