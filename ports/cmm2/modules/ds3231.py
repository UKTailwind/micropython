# ds3231: the Pico Computer 3's ds3231 module on the Colour Maximite 2, with the same names and behaviour. The CMM2's
# clock is the firmware's: set through MMBasic's DATE$ and TIME$ (which also set the DS3231 when OPTION DS3231 is ON)
# and read from its running clock. The daily alarm is the firmware's too, through the cmm2 module (the loader's
# rtc_alarm, services version 21): the DS3231's alarm 1 when one is fitted, else the H743's own RTC's alarm.
#
#   settime(y, mo, d, h, mi, s)  set the clock (and the DS3231)
#   settime()                    copy the current system clock to the DS3231 (the same clock here)
#   gettime()                    read the clock -> (y, mo, d, h, mi, s)
#   synctime()                   the system clock from the DS3231 (the firmware keeps them together) -> the time
#
# Daily alarm, as on a PC3:
#   set_alarm(h, mi, s=0)        arm a daily alarm; it fires at h:mi:s
#   alarm_fired()                has it gone off since the last clear?
#   clear_alarm()                acknowledge: clear the flag
#   alarm_off()                  disarm the alarm entirely
#   alarm_pin()                  the alarm as an input pin: reads 0 while it has fired, and irq() calls a handler when
#                                it fires. The CMM2's INT line is not on the I/O connector, so the flag is looked at
#                                every 250 ms instead (a soft timer), which a program cannot tell from a pin.

import time
import cmm2

INT_PIN = None  # (not on the CMM2's connector: alarm_pin() polls the alarm's flag)


def settime(year=None, month=1, day=1, hour=0, minute=0, second=0):
    """Set the clock (and the DS3231). With no arguments, copies the current
    system clock to the DS3231 instead (on the CMM2 they are one)."""
    if year is None:
        year, month, day, hour, minute, second = time.localtime()[:6]
    cmm2.basic('DATE$ = "%02d-%02d-%04d"' % (day, month, year))
    cmm2.basic('TIME$ = "%02d:%02d:%02d"' % (hour, minute, second))


def gettime():
    """Read the clock and return (year, month, day, hour, minute, second)."""
    return tuple(time.localtime()[:6])


def synctime():
    """The system clock from the DS3231: the CMM2's firmware keeps them
    together, so this returns the time."""
    return gettime()


def set_alarm(hour, minute, second=0):
    """Arm the daily alarm: at hour:minute:second EVERY day the fired flag
    sets. Acknowledge with clear_alarm()."""
    cmm2.alarm(1, hour, minute, second)


def alarm_off():
    """Disarm the daily alarm."""
    cmm2.alarm(0)


def alarm_fired():
    """True if the alarm has gone off since the last clear_alarm()."""
    return bool(cmm2.alarm(2))


def clear_alarm():
    """Acknowledge the alarm: clear the fired flag (it fires again at the
    next daily match)."""
    cmm2.alarm(3)


class _AlarmPin:
    # the alarm as a pull-up input on the INT line would read: 0 from the moment it fires until clear_alarm()
    IRQ_FALLING = 4
    IRQ_RISING = 8

    def __init__(self):
        self._timer = None
        self._handler = None
        self._trigger = 0
        self._last = 1

    def value(self):
        return 0 if alarm_fired() else 1

    def __call__(self):
        return self.value()

    def _poll(self, t):
        v = self.value()
        if v != self._last:
            self._last = v
            if self._trigger & (self.IRQ_RISING if v else self.IRQ_FALLING):
                self._handler(self)

    def irq(self, handler=None, trigger=IRQ_FALLING | IRQ_RISING):
        import machine

        if self._timer is not None:
            self._timer.deinit()
            self._timer = None
        self._handler = handler
        if handler is None:
            return
        self._trigger = trigger
        self._last = self.value()
        self._timer = machine.Timer(-1, mode=machine.Timer.PERIODIC, period=250, callback=self._poll)


_pin = None


def alarm_pin():
    """The alarm as an input pin: reads 0 while it has fired (until
    clear_alarm()); .irq(handler, Pin.IRQ_FALLING) calls handler(pin) when it
    fires. Looked at every 250 ms: the CMM2's INT line is not on its connector."""
    global _pin
    if _pin is None:
        _pin = _AlarmPin()
    return _pin
