# touch: the Pico Computer 3's touch module's API, reporting no touch panel. The CMM2's firmware has its own USB touch
# driver; its touches are not yet given to programs, so query("PRESENT") is False and the rest 0 (as on a PC3 with no
# panel), and pcgui works from the mouse.


def query(what, slot=4):
    what = str(what).upper()
    if what == "PRESENT":
        return False
    return 0
