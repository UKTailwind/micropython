def pt(cx, cy, ang, dist):
    r = math.radians(ang)
    return cx + math.sin(r) * dist, cy - math.cos(r) * dist
