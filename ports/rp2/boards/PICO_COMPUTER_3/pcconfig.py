# Persistent settings for the Pico Computer 3, stored as JSON on the flash
# filesystem so they survive a reboot. _boot applies the saved keyboard layout
# and HDMI mode/clock at start-up; keymap()/screen() below apply *and* persist a
# change. Everything is wrapped in try/except so a missing/corrupt file (or a
# read-only FS) never blocks boot.

import json

_PATH = "/settings.json"
_cfg = None


def _load():
    global _cfg
    if _cfg is None:
        try:
            with open(_PATH) as f:
                _cfg = json.load(f)
        except Exception:
            _cfg = {}
    return _cfg


def get(key, default=None):
    return _load().get(key, default)


def set(key, value):
    c = _load()
    c[key] = value
    try:
        with open(_PATH, "w") as f:
            json.dump(c, f)
    except Exception:
        pass


def keymap(name=None):
    """Get the current USB keyboard layout, or set it (persisted across reboots).
    e.g. keymap("UK"); keymaps() lists the choices."""
    import keyboard

    if name is None:
        return keyboard.keymap()
    keyboard.keymap(name)  # validates + applies (raises on a bad name)
    set("keymap", name)
    return name


def screen(mode=None, clock=252):
    """Set the HDMI screen mode and clock, persisted across reboots, and restart
    the on-screen console. With no arguments, returns the saved (mode, clock).

    mode  = hdmi.RGB640 (640x480x8) / RGB320 (320x240x16) / RGB512 (1024x600x16)
    clock = 252 / 315 / 378 MHz, and only for RGB640 and RGB320; RGB512 is fixed
            at 252 MHz."""
    import hdmi

    if mode is None:
        return (get("hdmi_mode", hdmi.RGB640), get("hdmi_clock", 252))
    # Validate BEFORE deinit so a bad argument doesn't leave the screen blank.
    if clock not in (252, 315, 378):
        raise ValueError("clock must be 252, 315 or 378")
    if mode == hdmi.RGB512 and clock != 252:
        raise ValueError("RGB512 only supports 252 MHz")
    hdmi.deinit()
    hdmi.init(mode, clock)
    set("hdmi_mode", mode)
    set("hdmi_clock", clock)
    import pcconsole

    pcconsole.console()  # re-establish the console at the new geometry
