def polygon(t, sides, size=60):
    for _ in range(sides):
        t.forward(size)
        t.right(360 / sides)

polygon(t, 6)          # a hexagon, default size
polygon(t, 3, 200)     # a big triangle
