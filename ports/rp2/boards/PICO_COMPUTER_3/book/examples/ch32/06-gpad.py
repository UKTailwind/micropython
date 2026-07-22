# gpad.py -- a knob-and-button game controller on the I/O header.
#   import gpad
#   x = gpad.dial(0, 559)     # knob position, scaled to a range
#   if gpad.fire(): ...       # edge-detected button press
import machine

_knob = machine.ADC(Pin(40))
_button = Pin(1, Pin.IN, Pin.PULL_UP)
_fire_was = 1

def raw():
    """The knob, 0..65535 (noisy at the edges -- that's analogue
    life)."""
    return _knob.read_u16()

def dial(lo, hi):
    """The knob mapped onto lo..hi."""
    return lo + raw() * (hi - lo) // 65535

def held():
    """Is the button down right now?"""
    return _button.value() == 0

def fire():
    """True exactly once per press (the just-pressed pattern)."""
    global _fire_was
    now = _button.value()
    pressed = now == 0 and _fire_was == 1
    _fire_was = now
    return pressed

if __name__ == "__main__":
    import time
    print("controller test -- turn and click (Ctrl-C stops)")
    while True:
        bar = dial(0, 20)
        click = " CLICK!" if fire() else ""
        print(f"\r{'#' * bar:20}{click}   ", end="")
        time.sleep_ms(20)
