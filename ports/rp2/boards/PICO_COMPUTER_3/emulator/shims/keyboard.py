# PHASE-1 STAND-IN for the keyboard C module. In the terminal phase there is
# no USB keyboard -- input arrives through stdin/the REPL -- so keydown()
# reports nothing held and on_key() stores its handler unused. Phase 2 feeds
# the firmware's own keymap tables from SDL events instead of this file.
# Key-code constants match usb_keyboard.c exactly.

UP = 0x80
DOWN = 0x81
LEFT = 0x82
RIGHT = 0x83
INS = 0x84
DEL = 0x7F
HOME = 0x86
END = 0x87
PGUP = 0x88
PGDN = 0x89
ENTER = 10
ESC = 27
TAB = 9
BKSP = 8
F1 = 0x91
F2 = 0x92
F3 = 0x93
F4 = 0x94
F5 = 0x95
F6 = 0x96
F7 = 0x97
F8 = 0x98
F9 = 0x99
F10 = 0x9A
F11 = 0x9B
F12 = 0x9C

_KEYMAPS = ("US", "UK", "DE", "FR", "ES", "BE")
_keymap = "US"
_on_key = None
_on_usb = None


def keymaps():
    return _KEYMAPS


def keymap(name=None):
    global _keymap
    if name is None:
        return _keymap
    name = name.upper()
    if name not in _KEYMAPS:
        raise ValueError("unknown keymap")
    _keymap = name


def keydown(n=1):
    return 0


def on_key(handler=None):
    global _on_key
    _on_key = handler


def on_usb_event(handler=None):
    global _on_usb
    _on_usb = handler
