# PHASE-1 STAND-IN for the mouse C module: no mouse present, so pccursor and
# pcgui degrade exactly as on a machine without one. Phase 4 replaces this
# with SDL mouse events feeding the same query() API (MMBasic DEVICE(MOUSE)).

_speed = 1


def query(what, slot=1):
    what = str(what).upper()
    if what == "PRESENT":
        return False
    return 0


def speed(v=None):
    global _speed
    if v is None:
        return _speed
    _speed = v
