def polygon(t, sides, size=60):
    if sides < 3:
        raise ValueError(f"a polygon needs 3+ sides, got {sides}")
    for _ in range(sides):
        t.forward(size)
        t.right(360 / sides)
