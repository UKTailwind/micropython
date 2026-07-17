# Pico Computer 3 emulator: the standard unix set, plus the board's frozen
# Python modules (unchanged from the firmware) and the emulator shims that
# stand in for the firmware's hardware C modules.

include("$(PORT_DIR)/variants/manifest.py")
include("$(MPY_DIR)/extmod/asyncio")

# Board modules, frozen from the same sources the firmware freezes.
# Phase 1 (terminal): the shell stack. Later phases add the graphics/
# game/audio stack as their C backends arrive.
freeze(
    "$(MPY_DIR)/ports/rp2/boards/PICO_COMPUTER_3",
    (
        "pcshell.py",
        "pcfm.py",
        "pye.py",
        "pcconfig.py",
        "pcgfx.py",
    ),
)

# Hardware stand-ins (hdmi/machine/keyboard/...) and the emulator's boot.
freeze("$(MPY_DIR)/ports/rp2/boards/PICO_COMPUTER_3/emulator/shims")
