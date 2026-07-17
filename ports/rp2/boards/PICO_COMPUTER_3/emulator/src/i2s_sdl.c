/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * (Licence text as per ports/rp2/hdmi.c.)
 */

// machine.I2S for the PC emulator (_emuaudio.I2S), with the rp2 port's
// non-blocking contract that pcaudio.py depends on:
//
//   i2s = I2S(0, sck=.., ws=.., sd=.., mode=I2S.TX, bits=16,
//             format=I2S.STEREO/MONO, rate=hz, ibuf=bytes)
//   i2s.irq(cb)     -> non-blocking mode: write() returns immediately and cb
//                      fires (via the scheduler) when the written buffer has
//                      been fully absorbed -- "give me the next chunk"
//   i2s.write(buf)
//   i2s.deinit()    -> stop NOW, dropping the buffered tail (as aborting DMA)
//
// Backing: one SDL audio device per instance (opened at the requested rate/
// channels, S16), pulling from an ibuf-sized ring buffer. Underrun plays
// silence, exactly as the machine's DMA does. All the audible content --
// tones, the 4-voice synth, MOD, WAV/MP3/FLAC -- is rendered by the
// firmware's own audio.c and arrives here as PCM.

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "py/runtime.h"
#include "py/mphal.h"

#if MICROPY_HW_ENABLE_HDMI // same gate as the rest of the multimedia stack

#include <SDL.h>

typedef struct _emu_i2s_obj_t {
    mp_obj_base_t base;
    SDL_AudioDeviceID dev;
    uint8_t *ring;
    size_t ring_size;
    volatile size_t head;      // write position (VM side)
    volatile size_t tail;      // read position (SDL audio thread)
    size_t frame_bytes;        // bytes per audio frame (2 mono / 4 stereo)
    mp_obj_t callback;         // the irq handler (or mp_const_none)
    mp_obj_t pending_buf;      // buffer mid-absorption (rooted here), or NULL
    const uint8_t *pending_ptr;
    size_t pending_len;
    size_t pending_off;
    volatile bool cb_due;      // a consumed-write callback still needs scheduling
    bool active;
} emu_i2s_obj_t;

static const mp_obj_type_t emu_i2s_type;

// The live instance (pcaudio uses one at a time: fresh I2S per playback).
// The SDL audio thread only ever sets cb_due on it; the consumed-write
// callback is scheduled from the VM side by pc3emu_event_hook() -- calling
// mp_sched_schedule from a foreign pthread proved unreliable on the unix
// port (the entry can be orphaned by a race with a concurrent drain).
static struct _emu_i2s_obj_t *volatile active_i2s = NULL;

static size_t ring_used(emu_i2s_obj_t *self) {
    return (self->head + self->ring_size - self->tail) % self->ring_size;
}

static size_t ring_free(emu_i2s_obj_t *self) {
    return self->ring_size - 1 - ring_used(self);
}

static void ring_put(emu_i2s_obj_t *self, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        self->ring[self->head] = src[i];
        self->head = (self->head + 1) % self->ring_size;
    }
}

// Absorb as much of the pending write as fits; schedule the irq callback
// once it has been fully taken. Caller holds the device lock.
static void absorb_pending(emu_i2s_obj_t *self) {
    if (self->pending_buf != MP_OBJ_NULL) {
        size_t space = ring_free(self);
        size_t left = self->pending_len - self->pending_off;
        size_t n = (left < space) ? left : space;
        if (n < left) {
            // Partial absorb: whole frames only, or an underrun's silence
            // padding can land mid-sample and slip the stream by a byte.
            n -= n % self->frame_bytes;
        }
        ring_put(self, self->pending_ptr + self->pending_off, n);
        self->pending_off += n;
        if (self->pending_off >= self->pending_len) {
            self->pending_buf = MP_OBJ_NULL;
            self->cb_due = true;
        }
    }
    // The consumed-write callback is delivered by pc3emu_event_hook() on the
    // VM thread; here we only mark it due.
}

// Called from MICROPY_INTERNAL_EVENT_HOOK (VM thread, every event pump --
// delays, vsync waits, the REPL's input poll): deliver any due consumed-write
// callback. Retries until the scheduler accepts it, so a full queue can't
// silently kill pcaudio's feed chain.
void pc3emu_audio_poll(void) {
    emu_i2s_obj_t *self = (emu_i2s_obj_t *)active_i2s;
    if (self != NULL && self->cb_due && self->callback != mp_const_none) {
        if (mp_sched_schedule(self->callback, MP_OBJ_FROM_PTR(self))) {
            self->cb_due = false;
        }
    }
}

// The variant's MICROPY_INTERNAL_EVENT_HOOK (see mpconfigvariant.h).
void pc3emu_event_hook(void) {
    pc3emu_audio_poll();
}

static void sdl_audio_cb(void *userdata, Uint8 *stream, int len) {
    emu_i2s_obj_t *self = (emu_i2s_obj_t *)userdata;
    size_t used = ring_used(self);
    size_t n = ((size_t)len < used) ? (size_t)len : used;
    n -= n % self->frame_bytes; // whole frames only (see absorb_pending)
    for (size_t i = 0; i < n; i++) {
        stream[i] = self->ring[self->tail];
        self->tail = (self->tail + 1) % self->ring_size;
    }
    if (n < (size_t)len) {
        memset(stream + n, 0, (size_t)len - n); // underrun: silence
    }
    absorb_pending(self); // room just opened: take more of the pending write
}

static mp_obj_t emu_i2s_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_sck, ARG_ws, ARG_sd, ARG_mode, ARG_bits, ARG_format, ARG_rate, ARG_ibuf };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_sck, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_ws, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_sd, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_bits, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16} },
        { MP_QSTR_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_rate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 44100} },
        { MP_QSTR_ibuf, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16384} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all_kw_array(n_args - 1, n_kw, all_args + 1,
        MP_ARRAY_SIZE(allowed), allowed, args);
    if (args[ARG_bits].u_int != 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 16"));
    }

    emu_i2s_obj_t *self = mp_obj_malloc(emu_i2s_obj_t, &emu_i2s_type);
    self->ring_size = (size_t)args[ARG_ibuf].u_int;
    if (self->ring_size < 1024) {
        self->ring_size = 1024;
    }
    self->ring = malloc(self->ring_size); // SDL thread reads it: not GC heap
    if (self->ring == NULL) {
        mp_raise_OSError(ENOMEM);
    }
    self->head = self->tail = 0;
    self->callback = mp_const_none;
    self->pending_buf = MP_OBJ_NULL;
    self->cb_due = false;
    self->active = false;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        free(self->ring);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no audio (SDL init failed)"));
    }
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = (int)args[ARG_rate].u_int;
    want.format = AUDIO_S16SYS;
    want.channels = (args[ARG_format].u_int == 1) ? 2 : 1; // STEREO=1, MONO=0
    self->frame_bytes = want.channels * 2;
    want.samples = 1024;
    want.callback = sdl_audio_cb;
    want.userdata = self;
    self->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (self->dev == 0) {
        free(self->ring);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no audio device"));
    }
    self->active = true;
    active_i2s = self;
    SDL_PauseAudioDevice(self->dev, 0);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t emu_i2s_irq(mp_obj_t self_in, mp_obj_t handler) {
    emu_i2s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    SDL_LockAudioDevice(self->dev);
    self->callback = handler;
    SDL_UnlockAudioDevice(self->dev);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(emu_i2s_irq_obj, emu_i2s_irq);

static mp_obj_t emu_i2s_write(mp_obj_t self_in, mp_obj_t buf_in) {
    emu_i2s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) {
        mp_raise_OSError(EIO);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len == 0) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    if (self->callback != mp_const_none) {
        // Non-blocking: absorb what fits now; the audio callback takes the
        // rest and fires the handler when the whole buffer is in.
        SDL_LockAudioDevice(self->dev);
        self->pending_buf = buf_in; // rooted via self until fully absorbed
        self->pending_ptr = (const uint8_t *)bufinfo.buf;
        self->pending_len = bufinfo.len;
        self->pending_off = 0;
        absorb_pending(self);
        SDL_UnlockAudioDevice(self->dev);
        return MP_OBJ_NEW_SMALL_INT((mp_int_t)bufinfo.len);
    }
    // Blocking: copy in as space appears.
    size_t off = 0;
    while (off < bufinfo.len) {
        SDL_LockAudioDevice(self->dev);
        size_t space = ring_free(self);
        size_t left = bufinfo.len - off;
        size_t n = (left < space) ? left : space;
        ring_put(self, (const uint8_t *)bufinfo.buf + off, n);
        SDL_UnlockAudioDevice(self->dev);
        off += n;
        if (off < bufinfo.len) {
            mp_hal_delay_ms(2);
        }
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)bufinfo.len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(emu_i2s_write_obj, emu_i2s_write);

static mp_obj_t emu_i2s_deinit(mp_obj_t self_in) {
    emu_i2s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->active) {
        if (active_i2s == self) {
            active_i2s = NULL;
        }
        self->active = false;
        SDL_CloseAudioDevice(self->dev); // immediate: drops the buffered tail
        free(self->ring);
        self->ring = NULL;
        self->pending_buf = MP_OBJ_NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(emu_i2s_deinit_obj, emu_i2s_deinit);

static const mp_rom_map_elem_t emu_i2s_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&emu_i2s_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&emu_i2s_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&emu_i2s_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_TX), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_RX), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_STEREO), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_MONO), MP_ROM_INT(0) },
};
static MP_DEFINE_CONST_DICT(emu_i2s_locals, emu_i2s_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    emu_i2s_type,
    MP_QSTR_I2S,
    MP_TYPE_FLAG_NONE,
    make_new, emu_i2s_make_new,
    locals_dict, &emu_i2s_locals
    );

static const mp_rom_map_elem_t emuaudio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__emuaudio) },
    { MP_ROM_QSTR(MP_QSTR_I2S), MP_ROM_PTR(&emu_i2s_type) },
};
static MP_DEFINE_CONST_DICT(emuaudio_module_globals, emuaudio_module_globals_table);

const mp_obj_module_t emuaudio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&emuaudio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__emuaudio, emuaudio_module);

#endif // MICROPY_HW_ENABLE_HDMI
