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


def unset(key):
    """Remove a saved key (no-op if absent), persisting the change."""
    c = _load()
    if key in c:
        del c[key]
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


def _kbd_key(vid=None, pid=None):
    """"vvvv:pppp" for the given VID/PID, or for the keyboard that's plugged in
    (None if there isn't one)."""
    if vid is None:
        import keyboard

        id = keyboard.kbd_id()
        if id is None:
            return None
        vid, pid = id
    return "%04x:%04x" % (vid, pid)


def numlock(on=None):
    """Get the num-lock state, or set it for the keyboard that's plugged in --
    saved per keyboard, so each of yours keeps its own setting.

    A keyboard with no numeric keypad (a Raspberry Pi keyboard, most laptop-style
    boards) overlays one onto 7890/uiop/jkl;/m whenever num-lock is on: its own
    firmware does that, triggered by the num-lock LED we send it. Such a keyboard
    wants numlock(False); a full-size one wants the default, True. Nothing in the
    USB descriptors tells the two apart -- a Pi keyboard and a full-size Lenovo
    send byte-identical HID report descriptors -- so the answer is remembered per
    keyboard rather than detected. Simply pressing Num Lock saves it too."""
    import keyboard

    if on is None:
        return keyboard.numlock()
    on = bool(on)
    keyboard.numlock(on)  # applies now and pushes the LEDs
    key = _kbd_key()
    if key:
        m = get("numlock", {})
        m[key] = on
        set("numlock", m)
    return on


def _numlock_saver(ev):
    """keyboard.on_numlock hook: the user pressed Num Lock, so remember what they
    chose for that keyboard. Scheduled, so keep it short and never raise."""
    try:
        vid, pid, on = ev
        m = get("numlock", {})
        m[_kbd_key(vid, pid)] = bool(on)
        set("numlock", m)
    except Exception:
        pass


def apply_numlock():
    """Push the saved per-keyboard num-lock settings down to the USB driver and
    arm the save hook. Called at boot, before any keyboard is mounted, so the
    first LED report a keyboard gets already carries its own setting."""
    try:
        import keyboard

        for k, v in get("numlock", {}).items():
            vid, _, pid = k.partition(":")
            keyboard.numlock_pref(int(vid, 16), int(pid, 16), bool(v))
        keyboard.on_numlock(_numlock_saver)
    except Exception:
        pass


def palette(index=None, rgb=None):
    """Get or set an RGB1024 palette entry, persisted across reboots. Only the
    RGB1024 (1024x600x4, RGB121 format) mode uses the palette.

    palette()            -> the 16 current entries as RGB888 ints
    palette(i)           -> entry i (0..15)
    palette(i, 0xRRGGBB) -> set entry i (and save all 16)"""
    import hdmi

    if index is None:
        return hdmi.palette()
    if rgb is None:
        return hdmi.palette(index)
    hdmi.palette(index, rgb)  # validates 0..15, applies immediately
    set("palette", list(hdmi.palette()))
    return rgb


def apply_palette():
    """Apply a saved palette (if any) to the hdmi module. Called at boot."""
    pal = get("palette")
    if pal:
        import hdmi

        for i, c in enumerate(pal[:16]):
            hdmi.palette(i, c)


def screen(mode=None, clock=252):
    """Set the HDMI screen mode and clock, persisted across reboots, and restart
    the on-screen console. With no arguments, returns the saved (mode, clock).

    mode  = hdmi.RGB640 (640x480x8) / RGB320 (320x240x16) / RGB512 (1024x600x16)
            / RGB1024 (1024x600x4, native 16-colour) / RGB640_4 (640x480x4,
            16-colour fast game mode) / RGB320_8 (320x240x8, layer + double
            buffer all in fast RAM)
    clock = 252 / 315 / 378 MHz for the 640x480 modes; RGB512 and RGB1024
            are fixed at 252 MHz."""
    import hdmi

    if mode is None:
        return (get("hdmi_mode", hdmi.RGB640), get("hdmi_clock", 252))
    # Validate BEFORE deinit so a bad argument doesn't leave the screen blank.
    if clock not in (252, 315, 378):
        raise ValueError("clock must be 252, 315 or 378")
    if mode in (hdmi.RGB512, hdmi.RGB1024) and clock != 252:
        raise ValueError("RGB512/RGB1024 only support 252 MHz")
    hdmi.deinit()
    hdmi.init(mode, clock)
    set("hdmi_mode", mode)
    set("hdmi_clock", clock)
    import pcconsole

    pcconsole.console()  # re-establish the console at the new geometry
