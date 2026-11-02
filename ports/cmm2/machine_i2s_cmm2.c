/*
 * machine.I2S on the Colour Maximite 2 G2: the Pico Computer 3's sound path (pcaudio.py over machine.I2S) on the
 * loader's sound stream, as the PC3 emulator's i2s_sdl.c puts it on an SDL audio device. The same non-blocking contract:
 *
 *   i2s = I2S(0, sck=.., ws=.., sd=.., mode=I2S.TX, bits=16, format=I2S.STEREO or MONO, rate=hz, ibuf=bytes)
 *   i2s.irq(cb)     non-blocking: write() returns at once, and cb is scheduled once the buffer is all taken
 *   i2s.write(buf)  (blocking without irq: returns once it is all taken)
 *   i2s.deinit()    stops at once, the buffered sound dropped
 *
 * The written sound goes into an ibuf-sized ring, which the stream's fill (audio_start: PendSV, about 44.1 kHz stereo)
 * empties, from the I2S's rate to the stream's with linear interpolation, a mono sample to both sides. An underrun is
 * silence, as the PC3's DMA plays it. One I2S plays at a time: a new one ends the last. The firmware's PLAY VOLUME
 * applies after pcaudio's own volume.
 *
 * machine.Pin is only a pin's number for now, for I2S's sck, ws and sd: the CMM2's I/O pins need services of their own.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "cmm2.h"

typedef struct _machine_i2s_obj_t {
    mp_obj_base_t base;
    uint8_t *ring;                          // (on the GC heap, kept by this object, which a root pointer keeps)
    uint32_t ring_size;
    volatile uint32_t head;                 // where write() puts the next byte
    volatile uint32_t tail;                 // where fill takes the next
    uint32_t frame_bytes;                   // 2 mono, 4 stereo
    uint32_t step;                          // I2S frames for each of the stream's, 16.16
    uint32_t acc;                           // the position between prev (0) and cur (0x10000)
    int32_t prev[2], cur[2];
    mp_obj_t callback;
    mp_obj_t pending_buf;                   // a non-blocking write not all taken (kept here till it is)
    const uint8_t *pending_ptr;
    uint32_t pending_len, pending_off;
    volatile bool cb_due;                   // a write all taken: its callback to be scheduled
    bool active;
} machine_i2s_obj_t;

MP_REGISTER_ROOT_POINTER(struct _machine_i2s_obj_t *cmm2_i2s);

static uint32_t ring_used(machine_i2s_obj_t *self) {
    return (self->head + self->ring_size - self->tail) % self->ring_size;
}

static uint32_t ring_free(machine_i2s_obj_t *self) {
    return self->ring_size - 4 - ring_used(self);   // (whole frames: a frame's room kept free)
}

static void ring_put(machine_i2s_obj_t *self, const uint8_t *src, uint32_t n) {
    uint32_t h = self->head, first = self->ring_size - h;
    if (first > n) {
        first = n;
    }
    memcpy(self->ring + h, src, first);
    memcpy(self->ring, src + first, n - first);
    self->head = (h + n) % self->ring_size;
}

// as much of the pending write as there is room for (fill, or write() with fill held off)
static void absorb_pending(machine_i2s_obj_t *self) {
    if (self->pending_buf == MP_OBJ_NULL) {
        return;
    }
    uint32_t left = self->pending_len - self->pending_off, n = ring_free(self);
    if (n >= left) {
        n = left;
    } else {
        n -= n % self->frame_bytes;
    }
    ring_put(self, self->pending_ptr + self->pending_off, n);
    self->pending_off += n;
    if (self->pending_off >= self->pending_len) {
        self->pending_buf = MP_OBJ_NULL;
        self->cb_due = true;
    }
}

static void next_frame(machine_i2s_obj_t *self) {
    self->prev[0] = self->cur[0];
    self->prev[1] = self->cur[1];
    if (ring_used(self) < self->frame_bytes) {
        self->cur[0] = self->cur[1] = 0;    // (an underrun: silence, nothing taken)
        return;
    }
    const int16_t *p = (const int16_t *)(self->ring + self->tail);
    self->cur[0] = p[0];
    self->cur[1] = self->frame_bytes == 4 ? p[1] : p[0];
    self->tail = (self->tail + self->frame_bytes) % self->ring_size;
}

// the stream's fill (PendSV): quick, and no services
static void i2s_fill(int16_t *buf, uint32_t frames) {
    machine_i2s_obj_t *self = MP_STATE_PORT(cmm2_i2s);
    if (self == NULL || !self->active) {
        memset(buf, 0, frames * 4);
        return;
    }
    while (frames--) {
        self->acc += self->step;
        while (self->acc >= 0x10000) {
            self->acc -= 0x10000;
            next_frame(self);
        }
        int32_t f = (int32_t)(self->acc >> 1);   // (15 bits, so the products fit)
        *buf++ = (int16_t)(self->prev[0] + (((self->cur[0] - self->prev[0]) * f) >> 15));
        *buf++ = (int16_t)(self->prev[1] + (((self->cur[1] - self->prev[1]) * f) >> 15));
    }
    absorb_pending(self);                   // (room has been made)
}

// a write all taken: its callback scheduled, from the VM (MICROPY_INTERNAL_EVENT_HOOK and the VM's hook)
void cmm2_i2s_poll(void) {
    machine_i2s_obj_t *self = MP_STATE_PORT(cmm2_i2s);
    if (self != NULL && self->cb_due && self->callback != mp_const_none) {
        if (mp_sched_schedule(self->callback, MP_OBJ_FROM_PTR(self))) {
            self->cb_due = false;
        }
    }
}

static void i2s_stop(machine_i2s_obj_t *self) {
    if (self->active) {
        self->active = false;
        cmm2->audio_stop();
        if (MP_STATE_PORT(cmm2_i2s) == self) {
            MP_STATE_PORT(cmm2_i2s) = NULL;
        }
        self->ring = NULL;
        self->pending_buf = MP_OBJ_NULL;
        self->cb_due = false;
    }
}

// a soft reset or the end of a script: the stream stopped before the heap goes
void cmm2_i2s_deinit_all(void) {
    if (MP_STATE_PORT(cmm2_i2s) != NULL) {
        i2s_stop(MP_STATE_PORT(cmm2_i2s));
    }
}

static mp_obj_t machine_i2s_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_sck, ARG_ws, ARG_sd, ARG_mck, ARG_mode, ARG_bits, ARG_format, ARG_rate, ARG_ibuf };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_sck, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_ws, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_sd, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_mck, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_bits, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16} },
        { MP_QSTR_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_rate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 44100} },
        { MP_QSTR_ibuf, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 16384} },
    };
    mp_arg_check_num(n_args, n_kw, 1, 1, true);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
    mp_arg_parse_all(n_args - 1, all_args + 1, &kw_args, MP_ARRAY_SIZE(allowed), allowed, args);
    if (mp_obj_get_int(all_args[0]) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("I2S 0 only"));
    }
    if (args[ARG_mode].u_int != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("TX only"));
    }
    if (args[ARG_bits].u_int != 16) {
        mp_raise_ValueError(MP_ERROR_TEXT("bits must be 16"));
    }
    mp_int_t rate = args[ARG_rate].u_int;
    if (rate < 1000 || rate > 192000) {
        mp_raise_ValueError(MP_ERROR_TEXT("rate"));
    }
    cmm2_i2s_deinit_all();                  // (one at a time)
    machine_i2s_obj_t *self = mp_obj_malloc(machine_i2s_obj_t, type);
    uint32_t size = (uint32_t)args[ARG_ibuf].u_int;
    self->ring_size = (size < 1024 ? 1024 : size) & ~3u;
    self->ring = m_new(uint8_t, self->ring_size);
    self->head = self->tail = 0;
    self->frame_bytes = args[ARG_format].u_int == 0 ? 2 : 4;
    self->acc = 0;
    self->prev[0] = self->prev[1] = self->cur[0] = self->cur[1] = 0;
    self->callback = mp_const_none;
    self->pending_buf = MP_OBJ_NULL;
    self->cb_due = false;
    self->active = true;
    self->step = 0x10000;
    MP_STATE_PORT(cmm2_i2s) = self;
    int out = cmm2->audio_start(i2s_fill);
    if (out <= 0) {
        self->active = false;
        MP_STATE_PORT(cmm2_i2s) = NULL;
        mp_raise_msg(&mp_type_OSError, out == -2 ? MP_ERROR_TEXT("the CMM2's sound is in use")
            : MP_ERROR_TEXT("no room for the sound's buffers"));
    }
    cmm2->audio_lock();
    self->step = (uint32_t)(((uint64_t)rate << 16) / (uint32_t)out);
    cmm2->audio_unlock();
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t machine_i2s_irq(mp_obj_t self_in, mp_obj_t handler) {
    machine_i2s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (handler != mp_const_none && !mp_obj_is_callable(handler)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid callback"));
    }
    self->callback = handler;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_i2s_irq_obj, machine_i2s_irq);

static mp_obj_t machine_i2s_write(mp_obj_t self_in, mp_obj_t buf_in) {
    machine_i2s_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->active) {
        mp_raise_OSError(MP_EIO);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len == 0) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    if (self->callback != mp_const_none) {
        cmm2->audio_lock();
        self->pending_buf = buf_in;
        self->pending_ptr = (const uint8_t *)bufinfo.buf;
        self->pending_len = bufinfo.len;
        self->pending_off = 0;
        absorb_pending(self);
        cmm2->audio_unlock();
        return MP_OBJ_NEW_SMALL_INT((mp_int_t)bufinfo.len);
    }
    uint32_t off = 0;
    while (off < bufinfo.len) {
        cmm2->audio_lock();
        uint32_t left = bufinfo.len - off, n = ring_free(self);
        if (n > left) {
            n = left;
        }
        ring_put(self, (const uint8_t *)bufinfo.buf + off, n);
        cmm2->audio_unlock();
        off += n;
        if (off < bufinfo.len) {
            mp_hal_delay_ms(2);
        }
    }
    return MP_OBJ_NEW_SMALL_INT((mp_int_t)bufinfo.len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_i2s_write_obj, machine_i2s_write);

static mp_obj_t machine_i2s_deinit(mp_obj_t self_in) {
    i2s_stop(MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_i2s_deinit_obj, machine_i2s_deinit);

static const mp_rom_map_elem_t machine_i2s_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_i2s_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&machine_i2s_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_i2s_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_TX), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_RX), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_STEREO), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_MONO), MP_ROM_INT(0) },
};
static MP_DEFINE_CONST_DICT(machine_i2s_locals, machine_i2s_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2s_type,
    MP_QSTR_I2S,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2s_make_new,
    locals_dict, &machine_i2s_locals
    );

// machine.Pin: a pin's number only, for now
typedef struct _machine_pin_obj_t {
    mp_obj_base_t base;
    mp_obj_t id;
} machine_pin_obj_t;

static mp_obj_t machine_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    if (n_args > 1 || n_kw > 0) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("the CMM2's I/O pins are not in MicroPython yet"));
    }
    machine_pin_obj_t *self = mp_obj_malloc(machine_pin_obj_t, type);
    self->id = args[0];
    return MP_OBJ_FROM_PTR(self);
}

static void machine_pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)kind;
    mp_print_str(print, "Pin(");
    mp_obj_print_helper(print, self->id, PRINT_REPR);
    mp_print_str(print, ")");
}

MP_DEFINE_CONST_OBJ_TYPE(
    machine_pin_type,
    MP_QSTR_Pin,
    MP_TYPE_FLAG_NONE,
    make_new, machine_pin_make_new,
    print, machine_pin_print
    );
