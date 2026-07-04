import vfs
import machine, rp2


# Try to mount the filesystem, and format the flash if it doesn't exist.
# Note: the flash requires the programming size to be aligned to 256 bytes.
bdev = rp2.Flash()
try:
    fs = vfs.VfsLfs2(bdev, progsize=256)
except:
    vfs.VfsLfs2.mkfs(bdev, progsize=256)
    fs = vfs.VfsLfs2(bdev, progsize=256)
vfs.mount(fs, "/")

# If the board provides a native SD card, try to mount it at /sd. Guarded by
# hasattr so this is a no-op on boards without machine.SDCard, and wrapped in
# try/except so a missing or unformatted card never blocks boot. The Pico
# Computer 3 does its own hot-swap-aware mounting via pcsd below, so skip it
# here on that board.
try:
    _is_pc3 = "PICO COMPUTER 3" in __import__("os").uname().machine
except Exception:
    _is_pc3 = False
if hasattr(machine, "SDCard") and not _is_pc3:
    try:
        _sd = machine.SDCard()
        if _sd.present():
            vfs.mount(vfs.VfsFat(_sd), "/sd")
        del _sd
    except Exception:
        pass

# On the Pico Computer 3, pre-import the most-used modules into the REPL
# (__main__) namespace so they're available without a manual import. _boot runs
# in its own module namespace, so we assign onto __main__ explicitly.
try:
    import os

    if "PICO COMPUTER 3" in os.uname().machine:
        import __main__
        import pcshell
        import hdmi
        import framebuf

        __main__.os = os
        __main__.machine = machine
        __main__.Pin = machine.Pin
        __main__.hdmi = hdmi
        __main__.framebuf = framebuf
        for _name in pcshell.COMMANDS:
            setattr(__main__, _name, getattr(pcshell, _name))
        # Graphics: Display class + the named colour palette (RED, WHITE, ...).
        import pcgfx

        __main__.Display = pcgfx.Display
        for _name in dir(pcgfx):
            if _name.isupper():
                setattr(__main__, _name, getattr(pcgfx, _name))
        # SD card: mount /sd (if a card is present) and start the background
        # hot-swap poll so cards can be inserted/removed while running.
        import pcsd

        pcsd.start()
        # On-screen console: console() mirrors REPL output to the HDMI screen.
        import pcconsole

        __main__.console = pcconsole.console
        # Persistent settings (keyboard layout, HDMI mode/clock) on the flash FS.
        import keyboard
        import pcconfig

        # keymap("UK")/screen(...) apply AND persist; keymaps() just lists.
        __main__.keymap = pcconfig.keymap
        __main__.keymaps = keyboard.keymaps
        __main__.screen = pcconfig.screen
        try:
            keyboard.keymap(pcconfig.get("keymap", "US"))  # apply saved layout
        except Exception:
            pass
        # Audio: WAV playback + volume over the PCM5102 I2S DAC.
        import pcaudio

        __main__.play = pcaudio.play
        __main__.volume = pcaudio.volume
        __main__.beep = pcaudio.beep
        __main__.stop = pcaudio.stop
        __main__.is_playing = pcaudio.is_playing
        # Play a short sound when a USB device is plugged in / removed.
        keyboard.on_usb_event(pcaudio.system_sound)
        # USB multi-touch: touch("X"), touch("SWIPE"), touch("TAP"), ... See
        # the `touch` module for the full list of subcommands (incl. gestures).
        import touch as _touch

        __main__.touch = _touch.query
        # USB mouse: mouse("X"), mouse("L"), mouse("W"), ... (MMBasic DEVICE(MOUSE)).
        import mouse as _mouse

        __main__.mouse = _mouse.query
        __main__.mouse_speed = _mouse.speed
        # DS3231 hardware RTC: set the system clock from it so the time is right
        # at boot. settime()/gettime()/synctime() are exposed for the user.
        import ds3231

        __main__.settime = ds3231.settime
        __main__.gettime = ds3231.gettime
        __main__.synctime = ds3231.synctime
        # Image loaders: draw_jpg() decodes a JPEG onto the HDMI screen.
        import pcimage

        __main__.draw_jpg = pcimage.draw_jpg
        __main__.draw_bmp = pcimage.draw_bmp
        __main__.draw_png = pcimage.draw_png
        __main__.save_image = pcimage.save_image
        try:
            ds3231.synctime()
        except Exception:
            pass  # no DS3231 / dead battery: leave the default clock
        # Bring up the HDMI display (saved mode/clock, default 640x480x8 @ 252)
        # and start the on-screen console. A bad saved value falls back safely.
        try:
            hdmi.init(pcconfig.get("hdmi_mode", hdmi.RGB640), pcconfig.get("hdmi_clock", 252))
        except Exception:
            hdmi.init(hdmi.RGB640)
        pcconsole.console()
        del __main__, _name
except Exception:
    pass

del vfs, bdev, fs
