# Emulator user-C-module: the REAL hdmi drawing core (shared with the
# firmware) + the SDL scanout backend. Pulled into the unix pc3 variant
# automatically when SDL2 is installed (see variants/pc3/mpconfigvariant.mk).

PC3EMU_DIR := $(USERMOD_DIR)
PC3_RP2_DIR := $(PC3EMU_DIR)/../../../..

SRC_USERMOD_C += $(PC3_RP2_DIR)/hdmi.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/draw3d.c
SRC_USERMOD_C += $(PC3EMU_DIR)/hdmi_sdl.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/usb_keyboard.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/kbd_decode.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/kbd_backend.c
SRC_USERMOD_C += $(PC3EMU_DIR)/kbd_sdl.c

# Mouse: the firmware's `mouse` module over SDL window-mouse state.
SRC_USERMOD_C += $(PC3_RP2_DIR)/usb_mouse_mod.c
SRC_USERMOD_C += $(PC3EMU_DIR)/mouse_sdl.c

# The virtual I/O-header panel (import Pins): switches/LEDs/pots window.
SRC_USERMOD_C += $(PC3EMU_DIR)/pins_sdl.c

# Audio: the firmware's renderer (synth/tones/MOD/WAV/MP3/FLAC -- pure pull
# code) + the SDL-backed machine.I2S lookalike it plays through.
# The unix CWARN (-Werror -Wdouble-promotion -Wfloat-conversion) lands after
# CFLAGS_USERMOD, so vendored float maths needs target-specific relaxation.
PC3_AUDIO_WNO = -Wno-float-conversion -Wno-double-promotion
$(BUILD)/src/../../../../audio.o: CWARN += $(PC3_AUDIO_WNO)
$(BUILD)/src/../../../../dr_wav.o: CWARN += $(PC3_AUDIO_WNO)
$(BUILD)/src/../../../../dr_mp3.o: CWARN += $(PC3_AUDIO_WNO)
$(BUILD)/src/../../../../dr_flac.o: CWARN += $(PC3_AUDIO_WNO)
$(BUILD)/src/../../../../hxcmod.o: CWARN += $(PC3_AUDIO_WNO)
$(BUILD)/src/../../../../usb_mouse_mod.o: CWARN += $(PC3_AUDIO_WNO)
# draw3d runs single-precision like MMBasic; double->float at the API
# boundary is intentional.
$(BUILD)/src/../../../../draw3d.o: CWARN += $(PC3_AUDIO_WNO)
SRC_USERMOD_C += $(PC3_RP2_DIR)/audio.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/dr_wav.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/dr_mp3.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/dr_flac.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/hxcmod.c
SRC_USERMOD_C += $(PC3EMU_DIR)/i2s_sdl.c

# ulab (numpy-like ndarrays), same submodule the firmware builds -- pcmath,
# plot() maths and the chapter-30 lab need it. Its micropython.mk snapshots
# USERMOD_DIR, so point it at ulab for the include. Guard the include so a
# fresh clone can still parse this file and run `make ... submodules` (which
# fetches lib/ulab); a hard include would kill make before it could fetch.
ifneq ($(wildcard $(TOP)/lib/ulab/code/micropython.mk),)
PC3_SAVED_USERMOD := $(USERMOD_DIR)
USERMOD_DIR := $(TOP)/lib/ulab/code
include $(TOP)/lib/ulab/code/micropython.mk
USERMOD_DIR := $(PC3_SAVED_USERMOD)
else
$(warning pc3: lib/ulab submodule not present -- run 'make -C ports/unix VARIANT=pc3 submodules' first (needs a git clone, not a source tarball), then rebuild)
endif

# usqlite (SQLite 3.47 amalgamation) -- same submodule the firmware bakes in, so
# `import usqlite` behaves identically in the emulator. Its micropython.mk
# snapshots USERMOD_DIR, so point it at lib/usqlite for the include (as ulab).
ifneq ($(wildcard $(TOP)/lib/usqlite/micropython.mk),)
PC3_SAVED_USERMOD := $(USERMOD_DIR)
USERMOD_DIR := $(TOP)/lib/usqlite
include $(TOP)/lib/usqlite/micropython.mk
USERMOD_DIR := $(PC3_SAVED_USERMOD)
# Vendored amalgamation: silence warnings for its objects. The unix CWARN
# (-Werror + strict float/extra checks) lands after CFLAGS_USERMOD, so relax
# per-object, exactly as the audio decoders above do.
$(foreach o,usqlite usqlite_module usqlite_connection usqlite_cursor usqlite_row \
            usqlite_file usqlite_mem usqlite_vfs usqlite_utils,\
    $(eval $(BUILD)/$(TOP)/lib/usqlite/$(o).o: CWARN := -w))
else
$(warning pc3: lib/usqlite submodule not present -- run 'make -C ports/unix VARIANT=pc3 submodules' first, then rebuild)
endif

# Image loaders (jpeg/bmp/png modules + decoders + dither): pure CPU code,
# drawing through the shared hdmi core.
SRC_USERMOD_C += $(PC3_RP2_DIR)/jpeg.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/picojpeg.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/bmp.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/bmp_decoder.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/png.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/upng.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/dither.c

# No -I$(PC3_RP2_DIR): that would put ports/rp2's mpconfigport.h ahead of the
# unix port's for every compile unit. hdmi.c resolves its own quoted includes
# (fonts.h, hdmi_priv.h) relative to itself; hdmi_sdl.c uses a relative path.
# Warning relaxations for the vendored decoders (picojpeg/bmp_decoder, from
# MMBasic): signed shifts and sign-compares that the arm toolchain accepts;
# unchanged from the firmware build.
CFLAGS_USERMOD += -DMICROPY_HW_ENABLE_HDMI=1 -DMICROPY_HW_USB_HOST=1 \
	-Wno-shift-negative-value -Wno-sign-compare \
	-Wno-float-conversion -Wno-double-promotion $(shell sdl2-config --cflags)
LDFLAGS_USERMOD += $(shell sdl2-config --libs) -lpthread
