include("$(PORT_DIR)/boards/manifest.py")

# Board boot hook: run at start-up by the shared rp2 _boot.py via its generic
# `import _boot_board` mechanism (pre-imports REPL names, mounts SD, HDMI, ...).
freeze("$(BOARD_DIR)", "_boot_board.py")

require("bundle-networking")

# MQTT client (publish/subscribe over TCP or TLS). "simple" is the minimal
# client; "robust" adds automatic reconnection on network errors.
require("umqtt.simple")
require("umqtt.robust")

# Bluetooth
require("aioble")

# SD card is provided by the native C machine.SDCard driver (machine_sdcard.c),
# so the pure-Python "sdcard" driver is not frozen in. pcsd.py mounts /sd and
# runs the hot-swap removal/insertion poll (replicates MMBasic's CheckSDCard).
freeze("$(BOARD_DIR)", "pcsd.py")

# USB flash drive support is provided by the native C machine.USBDrive driver
# (machine_usbdrive.c / usb_msc.c). pcusb.py mounts /usb and reacts to
# USBDrive.on_change -- USB enumeration gives a definitive plug/unplug event,
# so unlike pcsd.py this needs no poll.
freeze("$(BOARD_DIR)", "pcusb.py")

# BLE keyboard/mouse (HID over GATT) support -- scan/pair/connect on demand
# (unlike pcsd.py/pcusb.py, this doesn't auto-start at boot: pairing is a
# deliberate user action). Feeds reports into keyboard.inject_report() /
# mouse.inject_report(), the same decoders USB keyboards/mice use.
freeze("$(BOARD_DIR)", "pcblekbd.py")

# Shell-style REPL helpers (ls, ...), injected into __main__ by _boot.py.
freeze("$(BOARD_DIR)", "pcshell.py")

# Graphics: Display (framebuf subclass with RGB888 colour conversion) + palette.
freeze("$(BOARD_DIR)", "pcgfx.py")

# On-screen text console (mirrors REPL output to HDMI via os.dupterm).
freeze("$(BOARD_DIR)", "pcconsole.py")

# pye full-screen editor (robert-hh/Micropython-Editor, MIT, V2.79), exposed as
# edit() by pcshell/_boot. Vendored verbatim into the board directory.
freeze("$(BOARD_DIR)", "pye.py")

# WAV player + volume over the PCM5102 I2S DAC; exposes play_wav()/volume()/beep().
freeze("$(BOARD_DIR)", "pcaudio.py")

# DS3231 hardware RTC (I2C0 GP20/21); syncs the system clock at boot.
freeze("$(BOARD_DIR)", "ds3231.py")

# Image loaders (JPEG via picojpeg; BMP/PNG later) -> HDMI framebuffer.
freeze("$(BOARD_DIR)", "pcimage.py")

# Persistent settings (keyboard layout, HDMI mode/clock) saved on the flash FS.
freeze("$(BOARD_DIR)", "pcconfig.py")

# Sprite engine: MMBasic sprite semantics (layers, AABB collisions, walls,
# scroll) on a dirty-rectangle / overlay-layer compositor. `import pcsprite`.
freeze("$(BOARD_DIR)", "pcsprite.py")

# Wi-Fi + NTP time: wifi(), ntpsync(), tz(), auto() (credentials in settings).
freeze("$(BOARD_DIR)", "pcnet.py")

# Turtle graphics (MMBasic TURTLE, on the pcgfx primitives): `Turtle` class.
freeze("$(BOARD_DIR)", "pcturtle.py")

# Mouse pointer overlay (MMBasic GUI CURSOR): save-under arrow/cross sprite
# that tracks the USB mouse; auto-shown by pcgui. `import pccursor`.
freeze("$(BOARD_DIR)", "pccursor.py")

# On-screen GUI toolkit (MMBasic GUI controls): `import pcgui`; pcgui.GUI().
freeze("$(BOARD_DIR)", "pcgui.py")

# Tile maps (MMBasic TILEMAP): TileMap class + fast C render via hdmi.tilemap().
freeze("$(BOARD_DIR)", "pctilemap.py")

# Maths helpers over ulab (MMBasic MATH gaps): quaternions, vectors, DSP, PID.
freeze("$(BOARD_DIR)", "pcmath.py")

# Game-loop timing (MMBasic SYNC): `import pcgame`; pcgame.Clock(fps).
freeze("$(BOARD_DIR)", "pcgame.py")

# Simple plotting for education: plot(data) / plot(function, range).
freeze("$(BOARD_DIR)", "pcplot.py")

# Full-screen file manager (MMBasic FM): fm() -- browse/run/play/view/edit.
freeze("$(BOARD_DIR)", "pcfm.py")

# NOTE: SQLite is provided by the `usqlite` C user-module (lib/usqlite, wired in
# via USER_C_MODULES in mpconfigboard.cmake). It is pure C and self-registers, so
# there is no .py to freeze here -- `import usqlite` just works.
