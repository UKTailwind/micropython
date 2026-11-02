/*
 * _prof: a sampling profiler for working on this port. _prof.start() records, at each frame (the loader's frame hook,
 * 60-75 times a second), the PC the program was at when the frame began, from the exception frame on its stack
 * (PSP); _prof.stop() returns them as a list, to be matched with build/firmware.elf's symbols on the PC. Not while
 * the hdmi module's display is on (that owns the frame hook).
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/runtime.h"
#include "cmm2.h"

#define PROF_MAX 8192

static uint32_t prof_pc[PROF_MAX];
static volatile uint32_t prof_n;

static void prof_hook(void) {
    uint32_t psp;
    __asm volatile ("mrs %0, psp" : "=r" (psp));
    if (prof_n < PROF_MAX) {
        prof_pc[prof_n++] = ((const uint32_t *)psp)[6];   // (r0-r3, r12, lr, then the PC)
    }
}

static mp_obj_t prof_start(void) {
    prof_n = 0;
    cmm2->frame_hook(prof_hook);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(prof_start_obj, prof_start);

static mp_obj_t prof_stop(void) {
    cmm2->frame_hook(NULL);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (uint32_t i = 0; i < prof_n; i++) {
        mp_obj_list_append(list, mp_obj_new_int_from_uint(prof_pc[i]));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(prof_stop_obj, prof_stop);

// _prof.tex(n): MPU region 8's (the program's 8 MB) TEX field set to n (0: write-back without write-allocate, the
// firmware's; 1: with it), the data cache written back and emptied around the change; the field as it was returned
#define SCB_CCSIDR (*(volatile uint32_t *)0xE000ED80)
#define SCB_CSSELR (*(volatile uint32_t *)0xE000ED84)
#define SCB_DCCISW (*(volatile uint32_t *)0xE000EF74)
#define MPU_RNR (*(volatile uint32_t *)0xE000ED98)
#define MPU_RASR (*(volatile uint32_t *)0xE000EDA0)

static void dcache_clean_invalidate(void) {
    SCB_CSSELR = 0;
    __asm volatile ("dsb");
    uint32_t ccsidr = SCB_CCSIDR, sets = (ccsidr >> 13) & 0x7FFF;
    do {
        uint32_t ways = (ccsidr >> 3) & 0x3FF;
        do {
            SCB_DCCISW = ((sets << 5) & 0x3FE0) | ((ways << 30) & 0xC0000000);
        } while (ways-- != 0);
    } while (sets-- != 0);
    __asm volatile ("dsb\n isb");
}

static mp_obj_t prof_tex(mp_obj_t tex_in) {
    uint32_t tex = mp_obj_get_int(tex_in) & 7;
    __asm volatile ("cpsid i");
    dcache_clean_invalidate();
    MPU_RNR = 8;
    uint32_t rasr = MPU_RASR;
    MPU_RASR = (rasr & ~(7u << 19)) | (tex << 19);
    __asm volatile ("dsb\n isb");
    dcache_clean_invalidate();
    __asm volatile ("cpsie i");
    return MP_OBJ_NEW_SMALL_INT((rasr >> 19) & 7);
}
static MP_DEFINE_CONST_FUN_OBJ_1(prof_tex_obj, prof_tex);

static const mp_rom_map_elem_t prof_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__prof) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&prof_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&prof_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_tex), MP_ROM_PTR(&prof_tex_obj) },
};
static MP_DEFINE_CONST_DICT(prof_module_globals, prof_module_globals_table);

const mp_obj_module_t prof_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&prof_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__prof, prof_module);
