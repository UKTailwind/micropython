# board: which machine this is, with the Pico Computer 3's API (its board module tells a PC3 from a PC2), so code that
# asks runs unchanged and gets a straight answer here: a Colour Maximite 2 G2, with no Wi-Fi and no LED of its own.

UNKNOWN = 0
PICO_COMPUTER_3 = 1
PICO_COMPUTER_2 = 2
COLOUR_MAXIMITE_2 = 3

_NAMES = {
    UNKNOWN: "UNKNOWN",
    PICO_COMPUTER_3: "PICO COMPUTER 3",
    PICO_COMPUTER_2: "PICO COMPUTER 2",
    COLOUR_MAXIMITE_2: "COLOUR MAXIMITE 2 G2",
}

_id = COLOUR_MAXIMITE_2


def id():
    """Which machine this is: COLOUR_MAXIMITE_2."""
    return _id


def name():
    """Human-readable name of the board."""
    return _NAMES.get(_id, "UNKNOWN")


def has_wifi():
    """True if a Wi-Fi radio is fitted: never on the CMM2."""
    return False


def led_pin():
    """GPIO number of a user LED: the CMM2 has none (None)."""
    return None


def override(value):
    """Force the identity (to see how code behaves on another board)."""
    global _id
    if value not in _NAMES:
        raise ValueError("unknown board id")
    _id = value
