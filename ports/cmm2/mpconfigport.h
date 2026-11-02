/*
 * MicroPython for the Colour Maximite 2 G2 (a loadable program on the CMM2's firmware): the configuration.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <stdint.h>
#include <alloca.h>

// A full Python: the features rp2 and stm32 have, less the hardware this port has no access to yet.
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

#define MICROPY_HW_BOARD_NAME                   "Colour Maximite 2 G2"
#define MICROPY_HW_MCU_NAME                     "STM32H743"
#define MICROPY_PY_SYS_PLATFORM                 "cmm2"

// the Pico Computer 3's modules, on CMM2 back ends: hdmi (hdmi_cmm2.c), keyboard (kbd_cmm2.c) and mouse (mouse_cmm2.c)
#define MICROPY_HW_ENABLE_HDMI                  (1)
#define MICROPY_HW_USB_HOST                     (1)

// memory and the compiler
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_ENABLE_FINALISER                (1)
#define MICROPY_STACK_CHECK                     (1)
#define MICROPY_ALLOC_PATH_MAX                  (256)
#define MICROPY_GC_ALLOC_THRESHOLD              (0)
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_DOUBLE)   // (the M7 has a double-precision FPU)
#define MICROPY_ENABLE_SOURCE_LINE              (1)
#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_KBD_EXCEPTION                   (1)
#define MICROPY_HELPER_REPL                     (1)
#define MICROPY_REPL_AUTO_INDENT                (1)
#define MICROPY_REPL_EMACS_KEYS                 (1)
#define MICROPY_ENABLE_SCHEDULER                (1)
#define MICROPY_SCHEDULER_DEPTH                 (8)

// files: the loader's file services (vfs_cmm2.c), on the SD card (A:) and the USB drive (C:)
#define MICROPY_VFS                             (1)
#define MICROPY_READER_VFS                      (1)
#define MICROPY_PY_OS                           (1)
#define MICROPY_PY_OS_UNAME                     (1)
#define MICROPY_PY_OS_URANDOM                   (0)
#define MICROPY_PY_OS_STATVFS                   (0)
#define MICROPY_PY_IO                           (1)
#define MICROPY_PY_SYS_STDFILES                 (1)
#define MICROPY_PY_SYS_EXIT                     (1)

// time: the CMM2's clock and its microsecond timer
#define MICROPY_PY_TIME                         (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS            (1)
#define MICROPY_PY_TIME_INCLUDEFILE             "ports/cmm2/modtime.c"

// machine: what a loadable program can do (reset is back to MMBasic)
#define MICROPY_PY_MACHINE                      (1)
#define MICROPY_PY_MACHINE_INCLUDEFILE          "ports/cmm2/modmachine.c"
#define MICROPY_PY_MACHINE_RESET                (1)
#define MICROPY_PY_MACHINE_BARE_METAL_FUNCS     (1)
#define MICROPY_PY_MACHINE_PIN_BASE             (0)
#define MICROPY_PY_MACHINE_PULSE                (0)
#define MICROPY_PY_MACHINE_I2C                  (0)
#define MICROPY_PY_MACHINE_SOFTI2C              (0)
#define MICROPY_PY_MACHINE_SPI                  (0)
#define MICROPY_PY_MACHINE_SOFTSPI              (0)
#define MICROPY_PY_MACHINE_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_I2S), MP_ROM_PTR(&machine_i2s_type) }, \
    { MP_ROM_QSTR(MP_QSTR_Pin), MP_ROM_PTR(&machine_pin_type) },

#define MICROPY_PY_RANDOM_SEED_INIT_FUNC        (cmm2_random_seed())
#define MICROPY_PY_SELECT                       (1)
#define MICROPY_PY_THREAD                       (0)

// Ctrl-C (the loader's break_take) and the USB keyboard are seen to every so many VM steps, and while waiting
#define MICROPY_VM_HOOK_COUNT                   (64)
#define MICROPY_VM_HOOK_INIT                    static unsigned int vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL                    if (--vm_hook_divisor == 0) { \
        vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
        cmm2_vm_poll(); \
}
#define MICROPY_VM_HOOK_LOOP                    MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN                  MICROPY_VM_HOOK_POLL
#define MICROPY_INTERNAL_EVENT_HOOK             do { cmm2_vm_poll(); } while (0)

void cmm2_vm_poll(void);
uint32_t cmm2_random_seed(void);

#define MP_SSIZE_MAX                            (0x7fffffff)
typedef intptr_t mp_off_t;

#define MP_STATE_PORT                           MP_STATE_VM
