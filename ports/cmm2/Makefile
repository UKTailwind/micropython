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
CFLAGS += -g -O2 -DNDEBUG

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

SRC_S = shared/runtime/gchelper_thumb2.s

# the SDK's start-up and C library glue
SDK_O = $(BUILD)/sdk/crt0.o $(BUILD)/sdk/syscalls.o

SRC_QSTR += main.c mphalport.c vfs_cmm2.c vfs_cmm2_file.c modmachine.c modtime.c \
	shared/readline/readline.c shared/runtime/pyexec.c shared/runtime/sys_stdio_mphal.c

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_S:.s=.o))
OBJ += $(SDK_O)

all: $(BUILD)/micropython.app

$(BUILD)/sdk/%.o: $(CMM2_SDK)/%.c
	$(ECHO) "CC $<"
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS_MCU) -O2 -std=gnu11 -ffunction-sections -fdata-sections -I$(CMM2_SDK) -c $< -o $@

$(BUILD)/firmware.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(Q)$(SIZE) $@

$(BUILD)/micropython.app: $(BUILD)/firmware.elf
	$(ECHO) "Create $@"
	$(Q)$(PYTHON) $(CMM2_SDK)/mkimage.py $< $@ --name micropython --api 5 --stack 262144

include $(TOP)/py/mkrules.mk
