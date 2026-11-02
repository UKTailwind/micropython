# MicroPython for the Colour Maximite 2 G2, as a loadable program: RUN "micropython.app" at the CMM2's prompt.
#
# It runs on the CMM2's firmware through the loader's services (Inc/cmm2img.h in the CMM2V1.5-loader repository) and
# is linked with its SDK (tools/sdk: crt0.c, syscalls.c, app.ld, mkimage.py) at 0xD1800000, the G2's 8 MB for loadable
# programs. CMM2_SDK is that SDK's directory; build/micropython.app is the program.
#
#   make [CMM2_SDK=/path/to/CMM2V1.5-loader/tools/sdk]

include ../../py/mkenv.mk

CMM2_SDK ?= /mnt/d/Dropbox/VSCode-STM32/CMM2V1.5-loader/tools/sdk
CROSS_COMPILE ?= arm-none-eabi-

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

MICROPY_ROM_TEXT_COMPRESSION ?= 1

include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(CMM2_SDK)

CFLAGS_MCU = -mthumb -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard
CFLAGS += $(INC) -Wall -Werror -std=gnu99 $(CFLAGS_MCU) $(COPT)
CFLAGS += -fdata-sections -ffunction-sections -fno-common
CFLAGS += -g -O2 -DNDEBUG $(CFLAGS_EXTRA)

LDFLAGS = $(CFLAGS_MCU) --specs=nano.specs -nostartfiles -T $(CMM2_SDK)/app.ld \
	-Wl,--defsym=__app_base=0xD1800000 -Wl,--gc-sections -Wl,--emit-relocs \
	-Wl,-Map=$(BUILD)/firmware.map -Wl,--no-warn-rwx-segments
LIBS = -Wl,--start-group -lc -lm -lgcc -Wl,--end-group

SRC_C = \
	main.c \
	mphalport.c \
	vfs_cmm2.c \
	vfs_cmm2_file.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/runtime/sys_stdio_mphal.c \
	shared/runtime/interrupt_char.c \
	shared/runtime/gchelper_native.c \
	shared/timeutils/timeutils.c \
	hdmi_cmm2.c \
	kbd_cmm2.c \
	mouse_cmm2.c \
	machine_i2s_cmm2.c \

# the Pico Computer 3's modules, used unchanged from ports/rp2 (their objects in $(BUILD)/pc3): hdmi, draw3d,
# keyboard, mouse, audio (its decoders and synths) and the image loaders. The vendored decoders' signed shifts and
# sign compares are allowed, as the PC3's emulator allows them.
PC3_DIR = ../rp2
SRC_PC3 = hdmi.c draw3d.c usb_keyboard.c kbd_backend.c usb_mouse_mod.c \
	audio.c dr_wav.c dr_mp3.c dr_flac.c hxcmod.c \
	jpeg.c picojpeg.c bmp.c bmp_decoder.c png.c upng.c dither.c

SRC_S = shared/runtime/gchelper_thumb2.s

# the SDK's start-up and C library glue
SDK_O = $(BUILD)/sdk/crt0.o $(BUILD)/sdk/syscalls.o

SRC_QSTR += main.c mphalport.c vfs_cmm2.c vfs_cmm2_file.c modmachine.c modtime.c \
	shared/readline/readline.c shared/runtime/pyexec.c shared/runtime/sys_stdio_mphal.c \
	hdmi_cmm2.c kbd_cmm2.c mouse_cmm2.c machine_i2s_cmm2.c $(addprefix $(PC3_DIR)/, $(SRC_PC3))

# make PROF=1: with _prof (prof_cmm2.c), a sampling profiler for working on the port
ifeq ($(PROF),1)
SRC_C += prof_cmm2.c
SRC_QSTR += prof_cmm2.c
endif

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_S:.s=.o))
OBJ += $(addprefix $(BUILD)/pc3/, $(SRC_PC3:.c=.o))
OBJ += $(SDK_O)

all: $(BUILD)/micropython.app

$(BUILD)/sdk/%.o: $(CMM2_SDK)/%.c
	$(ECHO) "CC $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS_MCU) -O2 -std=gnu11 -ffunction-sections -fdata-sections -I$(CMM2_SDK) -c $< -o $@

$(BUILD)/pc3/%.o: CFLAGS += -Wno-shift-negative-value -Wno-sign-compare
$(BUILD)/pc3/%.o: $(PC3_DIR)/%.c
	$(call compile_c)

$(BUILD)/firmware.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(Q)$(SIZE) $@

$(BUILD)/micropython.app: $(BUILD)/firmware.elf
	$(ECHO) "Create $@"
	$(Q)$(PYTHON) $(CMM2_SDK)/mkimage.py $< $@ --name micropython --api 6 --stack 262144

include $(TOP)/py/mkrules.mk
