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
