# The virtual I/O header: `import Pins` opens a panel window with a switch
# and LED for every pin the Pico Computer 3 exposes (GP0-7, GP20, GP21,
# GP26, GP34-46) and a potentiometer on each analog pin (GP40-46).
#
#   - Click a switch to latch the input level machine.Pin reads (a Pin's
#     pull sets the idle level until you click).
#   - LEDs show a Pin's output when the pin is an OUTPUT, else the input.
#   - Drag a pot; machine.ADC(Pin(40)).read_u16() follows it.
#   - Pin.irq handlers fire on switch edges, as real wiring would.
#
#   Pins.close() shuts the panel; Pins.open() re-opens it.

import sys

try:
    import _emupins as _pp
except ImportError:
    raise ImportError("the pin panel needs the SDL emulator build")

import machine
import micropython

_prev = {}


def _dispatch(_t):
    # Deliver switch edges to Pin.irq handlers (runs on the Timer's schedule,
    # i.e. VM context -- like the GPIO IRQ handlers on the machine).
    while True:
        c = _pp.next_change()
        if c is None:
            return
        g, v = c
        _fire(g, v)


def _fire(g, v):
    p = machine.Pin._pins.get(str(g))
    old = _prev.get(g, 1 - v)
    _prev[g] = v
    if p is None or p._irq is None or p._irq[0] is None:
        return
    handler, trigger = p._irq
    rising = v and not old
    falling = old and not v
    if (rising and trigger & machine.Pin.IRQ_RISING) or \
       (falling and trigger & machine.Pin.IRQ_FALLING):
        try:
            handler(p)
        except Exception as e:
            sys.print_exception(e)


_timer = machine.Timer(-1, mode=machine.Timer.PERIODIC, period=30,
                       callback=_dispatch)


def open():
    _pp.show()


def close():
    _pp.hide()


_pp.show()
