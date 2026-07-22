# Pico Computer 3 emulator (see ports/rp2/boards/PICO_COMPUTER_3/emulator/).
# With SDL2 installed (apt install libsdl2-dev), the real hdmi drawing core +
# SDL scanout backend are compiled in; without it, the build is the phase-1
# terminal console (the frozen hdmi.py shim stands in).

FROZEN_MANIFEST ?= $(VARIANT_DIR)/manifest.py

ifneq ($(shell command -v sdl2-config 2>/dev/null),)
USER_C_MODULES = $(TOP)/ports/rp2/boards/PICO_COMPUTER_3/emulator
CFLAGS += -DPC3EMU_SDL=1
GIT_SUBMODULES += lib/ulab lib/usqlite
else
$(info pc3: SDL2 not found -- building the terminal-only emulator (no hdmi window))
endif
