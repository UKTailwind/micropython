/*
 * MicroPython for the Colour Maximite 2 G2: a loadable program on the CMM2's firmware.
 *
 *   RUN "micropython.app"              boot.py and main.py (if they are there), then the REPL
 *   RUN "micropython.app", script.py   the script, then back to MMBasic
 *
 * The heap is the loader's (the G2's 8 MB for programs, less the program and its stack), all but 1 MB of it the GC's.
 * The SD card (A:) is mounted at / and at /sd (as the Pico Computer 3 has its card), the USB drive (C:) at /usb when
 * there is one. Ctrl-D at the prompt is a soft reset, as on every MicroPython board; machine.reset() goes back to
 * MMBasic, which puts its screen back.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <stdio.h>
#include <string.h>
#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/cstack.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/vfs.h"
#include "shared/readline/readline.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "shared/runtime/softtimer.h"
#include "cmm2.h"
#include "vfs_cmm2.h"

extern void cmm2_i2s_deinit_all(void);      // machine_i2s_cmm2.c

#define CMM2_STACK_SIZE   (262144)          // as the Makefile's --stack
#define CMM2_NEWLIB_HEAP  (1024 * 1024)     // left to newlib's malloc

static void mount(const char *drive, qstr point) {
    mp_obj_t vfs = mp_vfs_cmm2_new(drive);
    mp_obj_t args[2] = { vfs, MP_OBJ_NEW_QSTR(point) };
    mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
}

static void mount_drives(void) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mount("A:", MP_QSTR__slash_);
        mount("A:", MP_QSTR__slash_sd);
        cmm2_stat st;
        if (cmm2->file_stat("C:/", &st) == 0) {
            mount("C:", MP_QSTR__slash_usb);
        }
        mp_vfs_chdir(MP_OBJ_NEW_QSTR(MP_QSTR__slash_));
        nlr_pop();
    } else {
        mp_printf(&mp_plat_print, "MPY: the drives could not be mounted\n");
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    }
}

int main(int argc, char **argv) {
    // the stack: the loader's, at the top of the 8 MB, CMM2_STACK_SIZE long
    char *stack_top = (char *)cmm2_start->heap + cmm2_start->heap_size + CMM2_STACK_SIZE;
    mp_cstack_init_with_top(stack_top, CMM2_STACK_SIZE);

    // the GC heap: the loader's heap but for newlib's share
    uint32_t heap_size = cmm2_start->heap_size - CMM2_NEWLIB_HEAP;
    char *heap = cmm2_heap_take(heap_size);
    if (heap == NULL) {
        printf("MPY: no room for the heap\n");
        return 1;
    }
    gc_init(heap, heap + heap_size);

    for (;;) {
        mp_init();
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
        readline_init0();
        mount_drives();
        soft_timer_init();
        // the CMM2's start-up (modules/_boot.py, frozen): the PC3's libraries ready at the REPL, the display and the
        // on-screen console up
        pyexec_frozen_module("_boot.py", false);

        if (argc > 1) {
            // a script named after RUN's comma: run it, then back to MMBasic
            int ret = pyexec_file(argv[1]);
            soft_timer_deinit();
            cmm2_i2s_deinit_all();
            gc_sweep_all();
            mp_deinit();
            return ret ? 0 : 1;
        }

        int ret = pyexec_file_if_exists("boot.py");
        if (ret & PYEXEC_FORCED_EXIT) {
            goto soft_reset_exit;
        }
        if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL && ret != 0) {
            ret = pyexec_file_if_exists("main.py");
            if (ret & PYEXEC_FORCED_EXIT) {
                goto soft_reset_exit;
            }
        }

        for (;;) {
            if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
                if (pyexec_raw_repl() != 0) {
                    break;
                }
            } else {
                if (pyexec_friendly_repl() != 0) {
                    break;
                }
            }
        }

    soft_reset_exit:
        mp_printf(MP_PYTHON_PRINTER, "MPY: soft reboot\n");
        soft_timer_deinit();
        cmm2_i2s_deinit_all();              // (the sound's fill reads the heap)
        gc_sweep_all();
        mp_deinit();
    }
}

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

void nlr_jump_fail(void *val) {
    printf("MPY: FATAL: uncaught exception %p\n", val);
    cmm2->leave(1);
    for (;;) {
    }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    cmm2->leave(1);
    for (;;) {
    }
}
#endif
