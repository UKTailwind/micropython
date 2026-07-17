# Emulator stand-in for the rp2 `machine` module.
#
# Virtual pins: every Pin holds a value; inputs with PULL_UP idle high,
# PULL_DOWN (or no pull) idle low, exactly what disconnected hardware reads.
# Tests / the future pin-panel UI can drive an input with pin.drive(v).
# ADC channels read mid-scale until set with machine.adc_set(gpio, raw).

import sys
import time

try:
    # The SDL-backed I2S (real audio out) when the emulator's C is built in.
    from _emuaudio import I2S
except ImportError:
    pass

try:
    # The virtual I/O-header panel (import Pins). When present, input Pins
    # read its switches, output Pins light its LEDs, ADC reads its pots.
    import _emupins as _panel
except ImportError:
    _panel = None


def _gpio(pin_id):
    # Numeric GPIO for panel routing, or None (named pins like "LED").
    try:
        return int(str(pin_id))
    except ValueError:
        return None


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
        g = _gpio(self._id)
        if _panel is not None and g is not None:
            _panel.set_output(g, self._mode == Pin.OUT)
            if self._mode == Pin.IN:
                _panel.idle_set(g, self._value)

    def value(self, v=None):
        if v is None:
            g = _gpio(self._id)
            if (_panel is not None and g is not None
                    and self._mode == Pin.IN and g != 32):
                return _panel.switch_get(g)  # 32 = DS3231 INT, chip-driven
            return self._value
        self._value = 1 if v else 0
        g = _gpio(self._id)
        if _panel is not None and g is not None and self._mode == Pin.OUT:
            _panel.led_set(g, self._value)

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
        g = _gpio(self._id)
        if _panel is not None and g is not None:
            return _panel.pot_get(g)  # the panel's potentiometer
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


def _bcd(v):
    return ((v // 10) << 4) | (v % 10)


def _unbcd(b):
    return (b >> 4) * 10 + (b & 0x0F)


class _DS3231:
    # Register-level DS3231 at 0x68, backed by the PC clock (offset shared
    # with machine.RTC, so settime()/RTC agree). Supports the board's use:
    # time regs 0x00-0x06 (BCD), Alarm 1 regs 0x07-0x0A (daily, A1M4 set),
    # control 0x0E (INTCN|A1IE), status 0x0F (A1F). The INT line (GP32,
    # open-drain active low) is mirrored onto the virtual Pin(32) whenever
    # the chip is accessed.

    def __init__(self):
        self.regs = bytearray(0x13)
        self.regs[0x0E] = 0x04  # power-up: INTCN set (alarms drive INT)
        self._next_fire = None

    def _now(self):
        return time.time() + RTC._offset

    def _encode_time(self):
        t = time.localtime(self._now())
        self.regs[0:7] = bytes((
            _bcd(t[5]), _bcd(t[4]), _bcd(t[3]),      # sec min hour (24 h)
            _bcd(t[6] + 1), _bcd(t[2]), _bcd(t[1]),  # dow date month
            _bcd(t[0] % 100),
        ))

    def _arm(self):
        # Daily alarm from the Alarm-1 registers, when enabled.
        if (self.regs[0x0E] & 0x05) != 0x05:  # need INTCN|A1IE
            self._next_fire = None
            return
        s = _unbcd(self.regs[0x07] & 0x7F)
        m = _unbcd(self.regs[0x08] & 0x7F)
        h = _unbcd(self.regs[0x09] & 0x3F)
        t = time.localtime(self._now())
        target = time.mktime((t[0], t[1], t[2], h, m, s, 0, 0))
        if target <= self._now():
            target += 86400
        self._next_fire = target

    def _tick(self):
        if self._next_fire is not None and self._now() >= self._next_fire:
            self.regs[0x0F] |= 0x01  # A1F
            self._next_fire += 86400  # daily
        # INT is open-drain active low while A1F and the enables are set.
        asserted = bool(self.regs[0x0F] & 0x01) and (self.regs[0x0E] & 0x05) == 0x05
        Pin(32, Pin.IN, Pin.PULL_UP)._value = 0 if asserted else 1

    def read(self, reg, n):
        if reg <= 0x06:
            self._encode_time()
        self._tick()
        return bytes(self.regs[reg:reg + n])

    def write(self, reg, data):
        self.regs[reg:reg + len(data)] = bytes(data)
        if reg == 0x00 and len(data) >= 7:
            # Setting the time moves the emulated clock's offset.
            y = 2000 + _unbcd(self.regs[6])
            target = time.mktime((y, _unbcd(self.regs[5] & 0x1F),
                                  _unbcd(self.regs[4] & 0x3F),
                                  _unbcd(self.regs[2] & 0x3F),
                                  _unbcd(self.regs[1] & 0x7F),
                                  _unbcd(self.regs[0] & 0x7F), 0, 0))
            RTC._offset = target - time.time()
        if 0x07 <= reg <= 0x0A or reg == 0x0E or reg == 0x0F:
            self._arm()
        self._tick()


_ds3231 = _DS3231()


class I2C:
    # I2C0 carries the DS3231 at 0x68 (as the machine's QWIIC/system bus).
    def __init__(self, bus_id=0, scl=None, sda=None, freq=400000):
        pass

    def scan(self):
        return [0x68]

    def readfrom_mem(self, addr, reg, n):
        if addr == 0x68:
            return _ds3231.read(reg, n)
        raise OSError(19)  # ENODEV, as an absent chip reads on hardware

    def writeto_mem(self, addr, reg, buf):
        if addr == 0x68:
            _ds3231.write(reg, bytes(buf))
            return
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
