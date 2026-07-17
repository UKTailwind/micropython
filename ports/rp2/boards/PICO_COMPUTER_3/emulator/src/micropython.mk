# Emulator user-C-module: the REAL hdmi drawing core (shared with the
# firmware) + the SDL scanout backend. Pulled into the unix pc3 variant
# automatically when SDL2 is installed (see variants/pc3/mpconfigvariant.mk).

PC3EMU_DIR := $(USERMOD_DIR)
PC3_RP2_DIR := $(PC3EMU_DIR)/../../../..

SRC_USERMOD_C += $(PC3_RP2_DIR)/hdmi.c
SRC_USERMOD_C += $(PC3EMU_DIR)/hdmi_sdl.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/usb_keyboard.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/kbd_decode.c
SRC_USERMOD_C += $(PC3EMU_DIR)/kbd_sdl.c

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
SRC_USERMOD_C += $(PC3_RP2_DIR)/audio.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/dr_wav.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/dr_mp3.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/dr_flac.c
SRC_USERMOD_C += $(PC3_RP2_DIR)/hxcmod.c
SRC_USERMOD_C += $(PC3EMU_DIR)/i2s_sdl.c

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
