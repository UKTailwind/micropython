# PHASE-1 STAND-IN for the touch C module: no touch panel present. Phase 4
# can map SDL mouse/touch events onto the same query() API.


def query(what, slot=4):
    what = str(what).upper()
    if what == "PRESENT":
        return False
    return 0
