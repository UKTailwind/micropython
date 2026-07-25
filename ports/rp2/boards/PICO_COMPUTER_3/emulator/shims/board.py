# STAND-IN for the board C module (runtime board identification): the firmware
# probes GP27 for the DS3231's 32 kHz clock to tell a Pico Computer 3 from a
# Pico Computer 2. The emulator is always a Pico Computer 3, so this answers as
# one -- with the same API, so book/manual code and ds3231.alarm_pin() run
# unchanged.
#
# has_wifi() is the exception: it reports False even though the machine it
# emulates has the radio, because there is no networking here. Like the other
# shims (touch("PRESENT"), USBSerial.connected()), it answers for what is
# actually available rather than for what the hardware would have.

UNKNOWN = 0
PICO_COMPUTER_3 = 1
PICO_COMPUTER_2 = 2

_NAMES = {
    UNKNOWN: "UNKNOWN",
    PICO_COMPUTER_3: "PICO COMPUTER 3",
    PICO_COMPUTER_2: "PICO COMPUTER 2",
}

_id = PICO_COMPUTER_3


def id():
    """Which machine this is: PICO_COMPUTER_3 or PICO_COMPUTER_2."""
    return _id


def name():
    """Human-readable name of the detected board."""
    return _NAMES.get(_id, "UNKNOWN")


def has_wifi():
    """True if the CYW43 radio is fitted (never on the emulator)."""
    return False


def led_pin():
    """GPIO number of the LED, or None when it is on the CYW43 (as on a
    Pico Computer 3, which is what the emulator emulates)."""
    return 25 if _id == PICO_COMPUTER_2 else None


def override(value):
    """Force the identity. On hardware this is the escape hatch for a Pico
    Computer 3 whose DS3231 has stopped; here it just lets you see how code
    behaves on the other board."""
    global _id
    if value not in _NAMES:
        raise ValueError("unknown board id")
    _id = value
