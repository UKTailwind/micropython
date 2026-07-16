nx = px + dx
if not tm.collide(nx, py, 14, 14, mask=SOLID):
    px = nx
