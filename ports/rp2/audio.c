/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND...
 */

// Small C helpers for the Python audio layer (pcaudio.py). The perf-critical
// per-sample work (volume scaling) is here; parsing/streaming stays in Python.

#include <string.h>
#include "py/runtime.h"
#include "py/stream.h"

#define DR_WAV_NO_STDIO
#define DR_WAV_NO_SIMD
#include "dr_wav.h"

#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#include "dr_mp3.h"

#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

// USB plug-in / unplug sounds (vendored from MMBasic): full WAV files, each an
// 8-bit unsigned mono 8000 Hz PCM stream after a 44-byte header.
#include "usb_connect_sound.h"
#include "usb_remove_sound.h"

// Scale 16-bit little-endian PCM samples in place by an 8.8 fixed-point gain
// (0..256, where 256 = unity). Applied per audio chunk from Python.
static mp_obj_t audio_scale(mp_obj_t buf_in, mp_obj_t gain_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_RW);
    int gain = mp_obj_get_int(gain_in);
    size_t n = bufinfo.len / 2;
    int16_t *s = (int16_t *)bufinfo.buf;
    if (gain >= 256) {
        return mp_const_none; // unity, nothing to do
    }
    if (gain <= 0) {
        memset(bufinfo.buf, 0, bufinfo.len);
        return mp_const_none;
    }
    for (size_t i = 0; i < n; i++) {
        s[i] = (int16_t)(((int32_t)s[i] * gain) >> 8);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(audio_scale_obj, audio_scale);

// --- WAV/MP3 decoding via dr_wav/dr_mp3, streamed from a Python file object -
//
// One decoder active at a time (a single playback stream). The Python side
// keeps the file object referenced (in pcaudio._pb) so it isn't collected while
// we hold g_audio_file for the read/seek callbacks.

static mp_obj_t g_audio_file = MP_OBJ_NULL; // Python file for the active decoder

// Shared stream callbacks (dr_wav's and dr_mp3's read/seek protos match).
static size_t audio_on_read(void *pUserData, void *pBufferOut, size_t bytesToRead) {
    (void)pUserData;
    const mp_stream_p_t *sp = mp_get_stream(g_audio_file);
    uint8_t *out = pBufferOut;
    size_t total = 0;
    while (total < bytesToRead) {
        int err;
        mp_uint_t n = sp->read(g_audio_file, out + total, bytesToRead - total, &err);
        if (n == MP_STREAM_ERROR || n == 0) {
            break;
        }
        total += n;
    }
    return total;
}

// whence is an MP_SEEK_* value. dr_mp3/dr_flac use three origins (SET/CUR/END);
// collapsing END to SET breaks init on files that size the stream by seeking to
// the end (e.g. VBR / MPEG-2 MP3s).
static bool audio_do_seek(int offset, int whence) {
    const mp_stream_p_t *sp = mp_get_stream(g_audio_file);
    struct mp_stream_seek_t seek;
    seek.offset = offset;
    seek.whence = whence;
    int err;
    return sp->ioctl(g_audio_file, MP_STREAM_SEEK, (uintptr_t)(void *)&seek, &err) != MP_STREAM_ERROR;
}

// Allocator backed by the GC heap (PSRAM), not the tiny C heap whose malloc
// panics on exhaustion. dr_mp3 keeps a *persistent* input buffer across reads,
// so its blocks must stay reachable by the collector: we hold each live pointer
// in a rooted table (see MP_REGISTER_ROOT_POINTER below).
#define AUDIO_ALLOCS_N 16
static void *audio_cb_malloc(size_t sz, void *u) {
    (void)u;
    void *p = m_malloc_maybe(sz);
    if (p != NULL) {
        for (int i = 0; i < AUDIO_ALLOCS_N; i++) {
            if (MP_STATE_PORT(audio_allocs)[i] == NULL) {
                MP_STATE_PORT(audio_allocs)[i] = p;
                break;
            }
        }
    }
    return p;
}
static void audio_cb_free(void *p, void *u) {
    (void)u;
    if (p == NULL) {
        return;
    }
    for (int i = 0; i < AUDIO_ALLOCS_N; i++) {
        if (MP_STATE_PORT(audio_allocs)[i] == p) {
            MP_STATE_PORT(audio_allocs)[i] = NULL;
            break;
        }
    }
    m_free(p);
}
static void *audio_cb_realloc(void *p, size_t sz, void *u) {
    if (p == NULL) {
        return audio_cb_malloc(sz, u);
    }
    void *np = m_realloc_maybe(p, sz, true);
    if (np != NULL && np != p) {
        for (int i = 0; i < AUDIO_ALLOCS_N; i++) {
            if (MP_STATE_PORT(audio_allocs)[i] == p) {
                MP_STATE_PORT(audio_allocs)[i] = np;
                break;
            }
        }
    }
    return np;
}

// ---- WAV (dr_wav) ----
static drwav g_wav;
static bool g_wav_open = false;
static drwav_bool32 wav_on_seek(void *u, int offset, drwav_seek_origin origin) {
    (void)u;
    // dr_wav only uses start/current.
    return audio_do_seek(offset, origin == drwav_seek_origin_current ? MP_SEEK_CUR : MP_SEEK_SET);
}
static const drwav_allocation_callbacks WAV_ALLOC = {
    NULL, audio_cb_malloc, audio_cb_realloc, audio_cb_free,
};

// wav_open(fileobj) -> (channels, sample_rate). Decodes any PCM/float/ADPCM WAV.
static mp_obj_t audio_wav_open(mp_obj_t file_in) {
    if (g_wav_open) {
        drwav_uninit(&g_wav);
        g_wav_open = false;
    }
    g_audio_file = file_in;
    if (!drwav_init(&g_wav, audio_on_read, wav_on_seek, NULL, &WAV_ALLOC)) {
        g_audio_file = MP_OBJ_NULL;
        mp_raise_ValueError(MP_ERROR_TEXT("not a valid WAV file"));
    }
    g_wav_open = true;
    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(g_wav.channels),
        mp_obj_new_int(g_wav.sampleRate),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_wav_open_obj, audio_wav_open);

// wav_read(buf) -> bytes of 16-bit interleaved PCM decoded into buf (0 at EOF).
static mp_obj_t audio_wav_read(mp_obj_t buf_in) {
    if (!g_wav_open) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    drwav_uint64 cap_frames = bufinfo.len / (sizeof(drwav_int16) * g_wav.channels);
    drwav_uint64 got = drwav_read_pcm_frames_s16(&g_wav, cap_frames, (drwav_int16 *)bufinfo.buf);
    return mp_obj_new_int((mp_int_t)(got * g_wav.channels * sizeof(drwav_int16)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_wav_read_obj, audio_wav_read);

static mp_obj_t audio_wav_close(void) {
    if (g_wav_open) {
        drwav_uninit(&g_wav);
        g_wav_open = false;
    }
    g_audio_file = MP_OBJ_NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_wav_close_obj, audio_wav_close);

// ---- MP3 (dr_mp3) ----
static drmp3 g_mp3;
static bool g_mp3_open = false;
static drmp3_bool32 mp3_on_seek(void *u, int offset, drmp3_seek_origin origin) {
    (void)u;
    int whence = (origin == DRMP3_SEEK_CUR) ? MP_SEEK_CUR
        : (origin == DRMP3_SEEK_END) ? MP_SEEK_END : MP_SEEK_SET;
    return audio_do_seek(offset, whence);
}
static drmp3_bool32 mp3_on_tell(void *u, drmp3_int64 *pCursor) {
    (void)u;
    const mp_stream_p_t *sp = mp_get_stream(g_audio_file);
    struct mp_stream_seek_t seek;
    seek.offset = 0;
    seek.whence = MP_SEEK_CUR;
    int err;
    if (sp->ioctl(g_audio_file, MP_STREAM_SEEK, (uintptr_t)(void *)&seek, &err) == MP_STREAM_ERROR) {
        return DRMP3_FALSE;
    }
    *pCursor = seek.offset;
    return DRMP3_TRUE;
}
static const drmp3_allocation_callbacks MP3_ALLOC = {
    NULL, audio_cb_malloc, audio_cb_realloc, audio_cb_free,
};

static mp_obj_t audio_mp3_open(mp_obj_t file_in) {
    if (g_mp3_open) {
        drmp3_uninit(&g_mp3);
        g_mp3_open = false;
    }
    g_audio_file = file_in;
    if (!drmp3_init(&g_mp3, audio_on_read, mp3_on_seek, mp3_on_tell, NULL, NULL, &MP3_ALLOC)) {
        g_audio_file = MP_OBJ_NULL;
        mp_raise_ValueError(MP_ERROR_TEXT("not a valid MP3 file"));
    }
    g_mp3_open = true;
    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(g_mp3.channels),
        mp_obj_new_int(g_mp3.sampleRate),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_mp3_open_obj, audio_mp3_open);

static mp_obj_t audio_mp3_read(mp_obj_t buf_in) {
    if (!g_mp3_open) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    drmp3_uint64 cap_frames = bufinfo.len / (sizeof(drmp3_int16) * g_mp3.channels);
    drmp3_uint64 got = drmp3_read_pcm_frames_s16(&g_mp3, cap_frames, (drmp3_int16 *)bufinfo.buf);
    return mp_obj_new_int((mp_int_t)(got * g_mp3.channels * sizeof(drmp3_int16)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_mp3_read_obj, audio_mp3_read);

static mp_obj_t audio_mp3_close(void) {
    if (g_mp3_open) {
        drmp3_uninit(&g_mp3);
        g_mp3_open = false;
    }
    g_audio_file = MP_OBJ_NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_mp3_close_obj, audio_mp3_close);

// ---- FLAC (dr_flac) ----
static drflac *g_flac = NULL;
static drflac_bool32 flac_on_seek(void *u, int offset, drflac_seek_origin origin) {
    (void)u;
    int whence = (origin == DRFLAC_SEEK_CUR) ? MP_SEEK_CUR
        : (origin == DRFLAC_SEEK_END) ? MP_SEEK_END : MP_SEEK_SET;
    return audio_do_seek(offset, whence);
}
static drflac_bool32 flac_on_tell(void *u, drflac_int64 *pCursor) {
    (void)u;
    const mp_stream_p_t *sp = mp_get_stream(g_audio_file);
    struct mp_stream_seek_t seek;
    seek.offset = 0;
    seek.whence = MP_SEEK_CUR;
    int err;
    if (sp->ioctl(g_audio_file, MP_STREAM_SEEK, (uintptr_t)(void *)&seek, &err) == MP_STREAM_ERROR) {
        return DRFLAC_FALSE;
    }
    *pCursor = seek.offset;
    return DRFLAC_TRUE;
}
static const drflac_allocation_callbacks FLAC_ALLOC = {
    NULL, audio_cb_malloc, audio_cb_realloc, audio_cb_free,
};

static mp_obj_t audio_flac_open(mp_obj_t file_in) {
    if (g_flac != NULL) {
        drflac_close(g_flac);
        g_flac = NULL;
    }
    g_audio_file = file_in;
    g_flac = drflac_open(audio_on_read, flac_on_seek, flac_on_tell, NULL, &FLAC_ALLOC);
    if (g_flac == NULL) {
        g_audio_file = MP_OBJ_NULL;
        mp_raise_ValueError(MP_ERROR_TEXT("not a valid FLAC file"));
    }
    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(g_flac->channels),
        mp_obj_new_int(g_flac->sampleRate),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_flac_open_obj, audio_flac_open);

static mp_obj_t audio_flac_read(mp_obj_t buf_in) {
    if (g_flac == NULL) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    drflac_uint64 cap_frames = bufinfo.len / (sizeof(drflac_int16) * g_flac->channels);
    drflac_uint64 got = drflac_read_pcm_frames_s16(g_flac, cap_frames, (drflac_int16 *)bufinfo.buf);
    return mp_obj_new_int((mp_int_t)(got * g_flac->channels * sizeof(drflac_int16)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_flac_read_obj, audio_flac_read);

static mp_obj_t audio_flac_close(void) {
    if (g_flac != NULL) {
        drflac_close(g_flac);
        g_flac = NULL;
    }
    g_audio_file = MP_OBJ_NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_flac_close_obj, audio_flac_close);

// usb_sound(connect) -> (samples, rate). Decode one of the vendored USB sounds
// (connect true = plug-in, false = unplug) from its fixed 8-bit unsigned mono
// WAV into a fresh 16-bit signed STEREO bytearray (each source sample copied to
// both channels so it plays on both), ready to hand straight to machine.I2S.
static mp_obj_t audio_usb_sound(mp_obj_t connect_in) {
    bool connect = mp_obj_is_true(connect_in);
    const uint8_t *wav = connect ? ezyZip_wav : remove_wav;
    uint32_t wav_size = connect ? EZYZIP_WAV_SIZE : REMOVE_WAV_SIZE;
    uint32_t rate = connect ? EZYZIP_SAMPLE_RATE : REMOVE_SAMPLE_RATE;
    // Standard 44-byte PCM WAV header precedes the 8-bit unsigned samples.
    const uint8_t *pcm = wav + 44;
    uint32_t nsamp = wav_size - 44;
    // 16-bit signed stereo: 2 channels * 2 bytes per source sample.
    int16_t *out = m_new(int16_t, nsamp * 2);
    for (uint32_t i = 0; i < nsamp; i++) {
        int16_t s = (int16_t)((pcm[i] - 128) << 8); // 8-bit unsigned -> 16-bit signed
        out[i * 2] = s;     // left
        out[i * 2 + 1] = s; // right
    }
    mp_obj_t buf_obj = mp_obj_new_bytearray_by_ref(nsamp * 4, out);
    mp_obj_t tuple[2] = { buf_obj, mp_obj_new_int(rate) };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_usb_sound_obj, audio_usb_sound);

static const mp_rom_map_elem_t audio_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_audio) },
    { MP_ROM_QSTR(MP_QSTR_scale), MP_ROM_PTR(&audio_scale_obj) },
    { MP_ROM_QSTR(MP_QSTR_wav_open), MP_ROM_PTR(&audio_wav_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_wav_read), MP_ROM_PTR(&audio_wav_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_wav_close), MP_ROM_PTR(&audio_wav_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_open), MP_ROM_PTR(&audio_mp3_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_read), MP_ROM_PTR(&audio_mp3_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_mp3_close), MP_ROM_PTR(&audio_mp3_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_flac_open), MP_ROM_PTR(&audio_flac_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_flac_read), MP_ROM_PTR(&audio_flac_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_flac_close), MP_ROM_PTR(&audio_flac_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_sound), MP_ROM_PTR(&audio_usb_sound_obj) },
};
static MP_DEFINE_CONST_DICT(audio_module_globals, audio_module_globals_table);

const mp_obj_module_t audio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&audio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_audio, audio_module);

// Live decoder allocations, kept reachable by the GC (audio_cb_malloc/free).
MP_REGISTER_ROOT_POINTER(void *audio_allocs[16]);
