/*
 * MicroPython for the Colour Maximite 2 G2 (a loadable program on the CMM2's firmware): the configuration.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <stdint.h>
#include <alloca.h>

// A full Python: the features rp2 and stm32 have, less the hardware this port has no access to yet.
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

#define MICROPY_HW_BOARD_NAME                   "Colour Maximite 2"     // (G1 or G2: the same program runs on both)
#define MICROPY_HW_MCU_NAME                     "STM32H743"
#define MICROPY_PY_SYS_PLATFORM                 "cmm2"

// the Pico Computer 3's modules, on CMM2 back ends: hdmi (hdmi_cmm2.c), keyboard (kbd_cmm2.c) and mouse (mouse_cmm2.c)
#define MICROPY_HW_ENABLE_HDMI                  (1)
#define MICROPY_HW_USB_HOST                     (1)
#define MICROPY_PY_OS_DUPTERM                   (1)   // (pcconsole's on-screen console)
// the PC3's Python libraries and the start-up, frozen (manifest.py: the build defines MICROPY_MODULE_FROZEN_MPY)
#define MICROPY_PERSISTENT_CODE_LOAD            (1)

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
// `import ulab.numpy` (a built-in module's submodule by its dotted name): MicroPython has it on from FULL_FEATURES,
// above this port's EXTRA_FEATURES (and rp2's: the PC3 lacks it too). The PC3 book's chapter 31 imports so.
#define MICROPY_MODULE_BUILTIN_SUBPACKAGES      (1)

// @micropython.native, @micropython.viper and @micropython.asm_thumb (the PC3 book's chapter 35), as rp2 has them. The
// code is put in the GC heap in the SDRAM, from which the program itself runs; cmm2_commit_exec (mphalport.c) writes
// it back from the D-cache and drops the I-cache before it runs. Defining MP_PLAT_COMMIT_EXEC would stop the tracking
// of a loaded .mpy's native text on the GC heap (py/mpconfig.h assumes the port allocates it then): set as for the GC
// heap's allocator.
#define MICROPY_EMIT_THUMB                      (1)
#define MICROPY_EMIT_INLINE_THUMB               (1)
#define MICROPY_MAKE_POINTER_CALLABLE(p)        ((void *)((uintptr_t)(p) | 1))   // (Thumb code: bit 0 set, as rp2)
#define MP_PLAT_COMMIT_EXEC(buf, len, reloc)    cmm2_commit_exec((buf), (len), (reloc))
#define MICROPY_PERSISTENT_CODE_TRACK_FUN_DATA  (1)
#define MICROPY_PERSISTENT_CODE_TRACK_BSS_RODATA (0)
void *cmm2_commit_exec(void *buf, unsigned int len, void *reloc);

// files: the loader's file services (vfs_cmm2.c), on the SD card (A:) and the USB drive (C:)
#define MICROPY_VFS                             (1)
#define MICROPY_READER_VFS                      (1)
#define MICROPY_PY_OS                           (1)
#define MICROPY_PY_OS_UNAME                     (1)
#define MICROPY_PY_OS_URANDOM                   (1)   // (the H743's RNG: mphalport.c)
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
// I/O through the loader's services (version 7), by the PicoMite's GP numbers: Pin, ADC, PWM, I2C, SPI, UART
#define MICROPY_PY_MACHINE_PULSE                (1)
#define MICROPY_PY_MACHINE_I2C                  (1)
#define MICROPY_PY_MACHINE_SOFTI2C              (1)
#define MICROPY_PY_MACHINE_SPI                  (1)
#define MICROPY_PY_MACHINE_SOFTSPI              (1)
#define MICROPY_PY_MACHINE_ADC                  (1)
#define MICROPY_PY_MACHINE_ADC_INCLUDEFILE      "ports/cmm2/machine_adc.c"
#define MICROPY_PY_MACHINE_ADC_READ_UV          (1)
#define MICROPY_PY_MACHINE_PWM                  (1)
#define MICROPY_PY_MACHINE_PWM_INCLUDEFILE      "ports/cmm2/machine_pwm.c"
#define MICROPY_PY_MACHINE_UART                 (1)
#define MICROPY_PY_MACHINE_UART_INCLUDEFILE     "ports/cmm2/machine_uart.c"
#define MICROPY_PY_MACHINE_TIMER                (1)   // (soft timers, run from the VM's hook)
#define MICROPY_PY_MACHINE_WDT                  (1)   // (the firmware's WATCHDOG: machine_wdt.c)
#define MICROPY_PY_MACHINE_WDT_INCLUDEFILE      "ports/cmm2/machine_wdt.c"
#define MICROPY_PY_PENDSV_ENTER                 (void)0   // (their handler is the VM's hook's, not an
#define MICROPY_PY_PENDSV_EXIT                  (void)0   // interrupt's: nothing to hold off)
#define MICROPY_PY_MACHINE_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_Timer), MP_ROM_PTR(&machine_timer_type) }, \
    { MP_ROM_QSTR(MP_QSTR_I2S), MP_ROM_PTR(&machine_i2s_type) }, \
    { MP_ROM_QSTR(MP_QSTR_Pin), MP_ROM_PTR(&machine_pin_type) }, \
    { MP_ROM_QSTR(MP_QSTR_RTC), MP_ROM_PTR(&machine_rtc_type) },

#define MICROPY_PY_RANDOM_SEED_INIT_FUNC        (cmm2_random_seed())
#define MICROPY_PY_SELECT                       (1)

// the network: socket (modsocket_cmm2.c) on the firmware's Wi-Fi and lwIP (the loader's services, version 21), the
// network module's WLAN (modules/network.py), and tls on mbedTLS (the Makefile's MICROPY_PY_SSL and
// MICROPY_SSL_MBEDTLS, which define them for the compiler), as rp2 has them
#define MICROPY_TRACKED_ALLOC                   (1)   // (mbedTLS's allocations)
#define MICROPY_PY_SOCKET                       (1)
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
