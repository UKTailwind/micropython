# gamepad: the Pico Computer 3's gamepad module's API, reporting no gamepad. The CMM2's firmware has its own USB gamepad
# drivers; their state is not yet given to programs, so query("PRESENT") is 0, the hat idle and the rest 0 (as on a PC3
# with none plugged in). The button bits are the PC3's.

R = 1 << 0
START = 1 << 1
HOME = 1 << 2
SELECT = 1 << 3
L = 1 << 4
DOWN = 1 << 5
RIGHT = 1 << 6
UP = 1 << 7
LEFT = 1 << 8
R2 = 1 << 9
X = 1 << 10
A = 1 << 11
Y = 1 << 12
B = 1 << 13
L2 = 1 << 14
TOUCH = 1 << 15


def query(what, chan=0):
    what = str(what).upper()
    if what == "PRESENT":
        return 0
    if what == "H":
        return 0xFF  # hat idle
    if what == "RAW":
        return b""
    return 0


def configure(vid, pid, mapping):
    pass


def mask(chan, bits):
    pass


def monitor(on):
    pass
