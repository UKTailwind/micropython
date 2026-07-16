# shapes.py -- turtle shape library. import shapes; shapes.star(t, 5, 100)

def polygon(t, sides, size=60):
    for _ in range(sides):
        t.forward(size)
        t.right(360 / sides)

def star(t, points, size=100):
    for _ in range(points):
        t.forward(size)
        t.right(180 - 180 / points)

def flower(t, petals, size=60):
    for _ in range(petals):
        polygon(t, 4, size)
        t.right(360 / petals)

def burst(t, spokes, size=80):
    for _ in range(spokes):
        t.forward(size)
        t.back(size)
        t.right(360 / spokes)
