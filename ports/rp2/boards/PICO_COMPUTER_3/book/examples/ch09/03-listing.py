n = int(input("Sides? "))

t = Turtle()
t.reset()
for _ in range(n):
    t.forward(60)
    t.right(360 / n)
