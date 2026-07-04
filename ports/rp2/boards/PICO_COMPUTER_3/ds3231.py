# DS3231 hardware real-time clock on the Pico Computer 3 (I2C0: GP20=SDA,
# GP21=SCL). The DS3231 is battery-backed, so it keeps time across power cycles;
# _boot syncs the MicroPython system clock from it at start-up.
#
#   settime(y, mo, d, h, mi, s)  set the DS3231 (and the system clock)
#   settime()                    copy the current system clock to the DS3231
#   gettime()                    read the DS3231 -> (y, mo, d, h, mi, s)
#   synctime()                   read the DS3231 -> set the system clock

import machine
import time

_ADDR = 0x68
_SDA = 20
_SCL = 21

_i2c = None


def _bus():
    global _i2c
    if _i2c is None:
        _i2c = machine.I2C(0, sda=machine.Pin(_SDA), scl=machine.Pin(_SCL), freq=400000)
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
