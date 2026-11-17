# Frozen into micropython.app: the Pico Computer 3's Python libraries, unchanged from its board directory, and the
# CMM2's own start-up and stand-ins (modules/: _boot, board, gamepad, touch, and ds3231 for the CMM2's own DS3231).
# Left out: pcsd and pcusb (the CMM2's firmware mounts the card and the USB drive) and the PC3's _boot_board
# (modules/_boot.py is the CMM2's).
freeze(
    "$(PORT_DIR)/../rp2/boards/PICO_COMPUTER_3",
    (
        "pcshell.py",
        "pcgfx.py",
        "pcconsole.py",
        "pye.py",
        "pcaudio.py",
        "pcimage.py",
        "pcconfig.py",
        "pcsprite.py",
        "pcturtle.py",
        "pccursor.py",
        "pcgui.py",
        "pctilemap.py",
        "pcmath.py",
        "pcgame.py",
        "pcplot.py",
        "pcfm.py",
        "pcnet.py",
    ),
)
freeze("$(PORT_DIR)/modules")

# asyncio, as rp2 freezes it for every board (its C half, _asyncio, is built in)
include("$(MPY_DIR)/extmod/asyncio")

# the network's libraries, as the PC3 has them (on the socket and tls modules and network.py): mip, ntptime, ssl,
# requests, webrepl and urequests; MQTT
require("bundle-networking")
require("umqtt.simple")
require("umqtt.robust")
