# Frozen into micropython.app: the Pico Computer 3's Python libraries, unchanged from its board directory, and the
# CMM2's own start-up and stand-ins (modules/). Left out: pcsd and pcusb (the CMM2's firmware mounts the card and the
# USB drive), pcnet (no network) and the PC3's _boot_board (modules/_boot.py is the CMM2's).
freeze(
    "$(PORT_DIR)/../rp2/boards/PICO_COMPUTER_3",
    (
        "pcshell.py",
        "pcgfx.py",
        "pcconsole.py",
        "pye.py",
        "pcaudio.py",
        "ds3231.py",
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
    ),
)
freeze("$(PORT_DIR)/modules")
