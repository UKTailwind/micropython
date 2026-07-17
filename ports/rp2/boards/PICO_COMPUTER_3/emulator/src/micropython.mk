# Emulator user-C-module: the REAL hdmi drawing core (shared with the
# firmware) + the SDL scanout backend. Pulled into the unix pc3 variant
# automatically when SDL2 is installed (see variants/pc3/mpconfigvariant.mk).

PC3EMU_DIR := $(USERMOD_DIR)
PC3_RP2_DIR := $(PC3EMU_DIR)/../../../..

SRC_USERMOD_C += $(PC3_RP2_DIR)/hdmi.c
SRC_USERMOD_C += $(PC3EMU_DIR)/hdmi_sdl.c

# No -I$(PC3_RP2_DIR): that would put ports/rp2's mpconfigport.h ahead of the
# unix port's for every compile unit. hdmi.c resolves its own quoted includes
# (fonts.h, hdmi_priv.h) relative to itself; hdmi_sdl.c uses a relative path.
CFLAGS_USERMOD += -DMICROPY_HW_ENABLE_HDMI=1 $(shell sdl2-config --cflags)
LDFLAGS_USERMOD += $(shell sdl2-config --libs) -lpthread
