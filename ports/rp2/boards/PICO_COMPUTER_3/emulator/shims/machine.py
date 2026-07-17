# Emulator stand-in for the rp2 `machine` module.
#
# Virtual pins: every Pin holds a value; inputs with PULL_UP idle high,
# PULL_DOWN (or no pull) idle low, exactly what disconnected hardware reads.
# Tests / the future pin-panel UI can drive an input with pin.drive(v).
# ADC channels read mid-scale until set with machine.adc_set(gpio, raw).

import sys
import time


class Pin:
    IN = 0
    OUT = 1
    OPEN_DRAIN = 2
    ALT = 3
    PULL_UP = 1
    PULL_DOWN = 2
    IRQ_RISING = 1
    IRQ_FALLING = 2

    _pins = {}  # id -> Pin (so Pin(5) everywhere is one virtual pin)

    def __new__(cls, pin_id, *args, **kwargs):
        key = str(pin_id)
        existing = cls._pins.get(key)
        if existing is None:
            existing = object.__new__(cls)
            existing._value = 0
            existing._mode = -1
            existing._pull = None
            existing._irq = None
            existing._id = key
            cls._pins[key] = existing
        return existing

    def __init__(self, pin_id, mode=-1, pull=None, value=None):
        if mode != -1:
            self._mode = mode
        if pull is not None or mode != -1:
            self._pull = pull
        if value is not None:
            self._value = 1 if value else 0
        elif self._mode == Pin.IN:
            self._value = 1 if self._pull == Pin.PULL_UP else 0

    def value(self, v=None):
        if v is None:
            return self._value
        self._value = 1 if v else 0

    def on(self):
        self.value(1)

    def off(self):
        self.value(0)

    def toggle(self):
        self.value(0 if self._value else 1)

    def irq(self, handler=None, trigger=3, hard=False):
        self._irq = (handler, trigger)

    def drive(self, v):
        # Emulator-only: an external signal changes an input pin.
        old = self._value
        self._value = 1 if v else 0
        if self._irq and self._irq[0]:
            handler, trigger = self._irq
            rising = self._value and not old
            falling = old and not self._value
            if (rising and trigger & Pin.IRQ_RISING) or (falling and trigger & Pin.IRQ_FALLING):
                handler(self)

    def __repr__(self):
        return "Pin({})".format(self._id)


_adc_raw = {}


def adc_set(pin_id, raw):
    # Emulator-only: set what an ADC channel reads (0..65535).
    _adc_raw[str(pin_id)] = raw


class ADC:
    def __init__(self, pin):
        self._id = pin._id if isinstance(pin, Pin) else str(pin)

    def read_u16(self):
        return _adc_raw.get(self._id, 32768)


class PWM:
    def __init__(self, pin, freq=1000, duty_u16=0):
        self._freq = freq
        self._duty = duty_u16

    def freq(self, f=None):
        if f is None:
            return self._freq
        self._freq = f

    def duty_u16(self, d=None):
        if d is None:
            return self._duty
        self._duty = d

    def deinit(self):
        pass


class I2C:
    # Phase-1 stub: an empty bus. Phase 4 adds the DS3231 at address 0x68
    # backed by the PC clock.
    def __init__(self, bus_id=0, scl=None, sda=None, freq=400000):
        pass

    def scan(self):
        return []

    def readfrom_mem(self, addr, reg, n):
        raise OSError(19)  # ENODEV, as an absent chip reads on hardware

    def writeto_mem(self, addr, reg, buf):
        raise OSError(19)

    def readfrom(self, addr, n):
        raise OSError(19)

    def writeto(self, addr, buf):
        raise OSError(19)


SoftI2C = I2C


class Timer:
    # One-shot / periodic callbacks on a background thread, delivered via
    # the scheduler like the hardware timer IRQ delivers them.
    ONE_SHOT = 0
    PERIODIC = 1

    def __init__(self, tim_id=-1, **kwargs):
        self._running = False
        if kwargs:
            self.init(**kwargs)

    def init(self, mode=PERIODIC, period=-1, freq=None, callback=None):
        import _thread
        import micropython

        self.deinit()
        if freq is not None:
            period = int(1000 / freq)
        if callback is None or period <= 0:
            return
        self._running = True

        def loop():
            while self._running:
                time.sleep_ms(period)
                if not self._running:
                    break
                try:
                    micropython.schedule(callback, self)
                except RuntimeError:
                    pass  # schedule queue full: drop, as hardware would
                if mode == Timer.ONE_SHOT:
                    break

        _thread.start_new_thread(loop, ())

    def deinit(self):
        self._running = False


class RTC:
    # Offset from the host clock; setting the time moves the offset, not
    # the PC. Shared with the DS3231 emulation (phase 4) so they agree.
    _offset = 0  # seconds

    def datetime(self, dt=None):
        if dt is None:
            t = time.localtime(time.time() + RTC._offset)
            # (year, month, day, weekday, hour, minute, second, subsecond)
            return (t[0], t[1], t[2], t[6], t[3], t[4], t[5], 0)
        target = time.mktime((dt[0], dt[1], dt[2], dt[4], dt[5], dt[6], 0, 0))
        RTC._offset = target - time.time()


def freq(f=None):
    if f is None:
        return 378000000  # what the board runs at
    # accept and ignore: the emulator's speed is the PC's


def unique_id():
    return b"PC3EMU\x00\x01"


def reset():
    print("PC3EMU: machine.reset() -- exiting (restart the emulator)")
    sys.exit(0)


def soft_reset():
    raise SystemExit


def idle():
    time.sleep_ms(1)


def lightsleep(ms=None):
    if ms:
        time.sleep_ms(ms)


def deepsleep(ms=None):
    reset()


def disable_irq():
    return 0


def enable_irq(state=0):
    pass


class WDT:
    def __init__(self, timeout=5000):
        pass

    def feed(self):
        pass
