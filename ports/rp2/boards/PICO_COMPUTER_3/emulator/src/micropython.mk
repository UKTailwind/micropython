# Emulator user-C-module: the REAL hdmi drawing core (shared with the
# firmware) + the SDL scanout backend. Pulled into the unix pc3 variant
# automatically when SDL2 is installed (see variants/pc3/mpconfigvariant.mk).

PC3EMU_DIR := $(USERMOD_DIR)
PC3_RP2_DIR := $(PC3EMU_DIR)/../../../..

SRC_USERMOD_C += $(PC3_RP2_DIR)/hdmi.c
SRC_USERMOD_C += $(PC3EMU_DIR)/hdmi_sdl.c

CFLAGS_USERMOD += -I$(PC3_RP2_DIR) -DMICROPY_HW_ENABLE_HDMI=1 $(shell sdl2-config --cflags)
LDFLAGS_USERMOD += $(shell sdl2-config --libs) -lpthread
