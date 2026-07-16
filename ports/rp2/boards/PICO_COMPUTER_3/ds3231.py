# DS3231 hardware real-time clock on the Pico Computer 3 (I2C0: GP20=SDA,
# GP21=SCL). The DS3231 is battery-backed, so it keeps time across power cycles;
# _boot syncs the MicroPython system clock from it at start-up.
#
#   settime(y, mo, d, h, mi, s)  set the DS3231 (and the system clock)
#   settime()                    copy the current system clock to the DS3231
#   gettime()                    read the DS3231 -> (y, mo, d, h, mi, s)
#   synctime()                   read the DS3231 -> set the system clock
#
# Daily alarm on the chip's INT pin (wired to GP32 on this board):
#   set_alarm(h, mi, s=0)        arm a daily alarm; INT goes low at h:mi:s
#   alarm_fired()                has it gone off since the last clear?
#   clear_alarm()                acknowledge: clear the flag, release INT
#   alarm_off()                  disarm the alarm entirely
#   alarm_pin()                  GP32 as a pulled-up input Pin (INT is
#                                open-drain: reads 0 while asserted)

import machine
import time

_ADDR = 0x68
_SDA = 20
_SCL = 21

_i2c = None
_i2c_freq = 0  # machine.freq() at which _i2c's baud divider was computed


def _bus():
    # The I2C baud divider is derived from the peripheral clock (which tracks
    # clk_sys) when the bus is created. A live CPU-clock change — screen(mode,
    # 315/378) — would otherwise leave a cached bus clocking SCL at the wrong
    # speed (too fast for the DS3231's 400 kHz limit when the clock rises), so
    # re-create the bus whenever clk_sys has changed since it was built.
    global _i2c, _i2c_freq
    f = machine.freq()
    if _i2c is None or f != _i2c_freq:
        _i2c = machine.I2C(0, sda=machine.Pin(_SDA), scl=machine.Pin(_SCL), freq=400000)
        _i2c_freq = f
    return _i2c


def _bcd2dec(b):
    return (b >> 4) * 10 + (b & 0x0F)


def _dec2bcd(d):
    return ((d // 10) << 4) | (d % 10)


def _weekday(year, month, day):
    # 0 = Monday .. 6 = Sunday (MicroPython convention), derived from the date.
    return time.localtime(time.mktime((year, month, day, 0, 0, 0, 0, 0)))[6]


def settime(year=None, month=1, day=1, hour=0, minute=0, second=0):
    """Set the DS3231 and the system clock. With no arguments, copies the
    current system clock to the DS3231 instead."""
    if year is None:
        lt = time.localtime()
        year, month, day, hour, minute, second, wday = lt[0], lt[1], lt[2], lt[3], lt[4], lt[5], lt[6]
    else:
        wday = _weekday(year, month, day)
    buf = bytes(
        (
            _dec2bcd(second),
            _dec2bcd(minute),
            _dec2bcd(hour),        # bit 6 = 0 -> 24-hour mode
            _dec2bcd(wday + 1),    # DS3231 day-of-week is 1..7
            _dec2bcd(day),
            _dec2bcd(month),       # century bit (0x80) left clear
            _dec2bcd(year % 100),
        )
    )
    _bus().writeto_mem(_ADDR, 0x00, buf)
    machine.RTC().datetime((year, month, day, wday, hour, minute, second, 0))


def gettime():
    """Read the DS3231 and return (year, month, day, hour, minute, second)."""
    d = _bus().readfrom_mem(_ADDR, 0x00, 7)
    second = _bcd2dec(d[0] & 0x7F)
    minute = _bcd2dec(d[1] & 0x7F)
    h = d[2]
    if h & 0x40:  # 12-hour mode
        hour = _bcd2dec(h & 0x1F) % 12
        if h & 0x20:  # PM
            hour += 12
    else:
        hour = _bcd2dec(h & 0x3F)
    day = _bcd2dec(d[4] & 0x3F)
    month = _bcd2dec(d[5] & 0x1F)
    year = 2000 + _bcd2dec(d[6])
    return (year, month, day, hour, minute, second)


def synctime():
    """Read the DS3231 and set the MicroPython system clock; returns the time."""
    year, month, day, hour, minute, second = gettime()
    wday = _weekday(year, month, day)
    machine.RTC().datetime((year, month, day, wday, hour, minute, second, 0))
    return (year, month, day, hour, minute, second)


# --- Daily alarm (Alarm 1) on the INT pin -----------------------------------
# The chip's INT/SQW output is wired to GP32 on this board. It is open-drain
# and active LOW: enable the internal pull-up (alarm_pin() does) and the line
# sits at 1, dropping to 0 when the alarm fires, until clear_alarm().

INT_PIN = 32

_CONTROL = 0x0E
_STATUS = 0x0F
_A1IE = 0x01      # alarm-1 interrupt enable (control)
_INTCN = 0x04     # INT pin carries alarms, not the square wave (control)
_A1F = 0x01       # alarm-1 fired flag (status)


def set_alarm(hour, minute, second=0):
    """Arm the daily alarm: at hour:minute:second EVERY day the fired flag
    sets and the INT pin (GP32) goes low. Acknowledge with clear_alarm()."""
    buf = bytes(
        (
            _dec2bcd(second),      # A1M1 = 0: match seconds
            _dec2bcd(minute),      # A1M2 = 0: match minutes
            _dec2bcd(hour),        # A1M3 = 0: match hours (24-hour)
            0x80,                  # A1M4 = 1: ignore the day -> daily
        )
    )
    bus = _bus()
    bus.writeto_mem(_ADDR, 0x07, buf)
    clear_alarm()                  # start with the flag down, INT released
    ctrl = bus.readfrom_mem(_ADDR, _CONTROL, 1)[0]
    bus.writeto_mem(_ADDR, _CONTROL, bytes((ctrl | _INTCN | _A1IE,)))


def alarm_off():
    """Disarm the daily alarm (and release INT if it was asserted)."""
    bus = _bus()
    ctrl = bus.readfrom_mem(_ADDR, _CONTROL, 1)[0]
    bus.writeto_mem(_ADDR, _CONTROL, bytes((ctrl & ~_A1IE & 0xFF,)))
    clear_alarm()


def alarm_fired():
    """True if the alarm has gone off since the last clear_alarm()."""
    return bool(_bus().readfrom_mem(_ADDR, _STATUS, 1)[0] & _A1F)


def clear_alarm():
    """Acknowledge the alarm: clear the fired flag and release the INT pin.
    (It will assert again at the next daily match.)"""
    bus = _bus()
    st = bus.readfrom_mem(_ADDR, _STATUS, 1)[0]
    bus.writeto_mem(_ADDR, _STATUS, bytes((st & ~_A1F & 0xFF,)))


def alarm_pin():
    """GP32 (the DS3231's INT line) as a ready-made input: pull-up enabled,
    reads 0 while the alarm is asserted. Use .irq() on it for interrupt
    wake-ups (User Manual, section 14)."""
    return machine.Pin(INT_PIN, machine.Pin.IN, machine.Pin.PULL_UP)
