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

// MOD tracker player (hxcmod, vendored from MMBasic third_party_mod — includes
// MMBasic's sound-effect extension used by PLAY MODSAMPLE).
#include "hxcmod.h"

// SineTable/triangletable (4096-entry, values 100..3900 centred on 2000) and
// mapping[101] (volume index -> amplitude), vendored verbatim from MMBasic.
#include "sound_tables.h"

#include <stdlib.h> // rand() for the noise generators (as MMBasic)

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

// --- Tone generator (MMBasic PLAY TONE / fillToneBuffer) --------------------
//
// Dual-channel sine tones from the 4096-entry SineTable via floating-point
// phase accumulators, at the fixed synth rate (MMBasic PWM_FREQ). Full-scale
// output: (table - 2000) * 16 = +/-30400; the master volume is applied by the
// Python pump (audio.scale), which plays the role of MMBasic's i2sconvert.

#define SYNTH_RATE 44100
#define TONE_FOREVER 0xffffffffffffffffULL

static float tone_phase_l, tone_phase_r; // 0..4096 into SineTable
static float tone_phinc_l, tone_phinc_r; // phase increment per sample
static uint64_t tone_remaining;          // stereo frames left (TONE_FOREVER = endless)
static bool tone_mono;                   // left == right: compute once (MMBasic)

// tone_start(f_left, f_right, ms, fresh). ms None/negative = play forever.
// fresh=True resets the phase accumulators (new playback); fresh=False updates
// frequency/duration of an already-running tone without a phase discontinuity
// (MMBasic's repeat-call path). Duration is rounded to a whole number of
// left-channel cycles (f >= 10 Hz) so the tone ends at a zero crossing.
static mp_obj_t audio_tone_start(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_float_t f_left = mp_obj_get_float(args[0]);
    mp_float_t f_right = mp_obj_get_float(args[1]);
    if (f_left < 0 || f_left > 22050 || f_right < 0 || f_right > 22050) {
        mp_raise_ValueError(MP_ERROR_TEXT("valid is 0Hz to 20KHz"));
    }
    uint64_t duration = TONE_FOREVER;
    if (args[2] != mp_const_none) {
        mp_float_t ms = mp_obj_get_float(args[2]);
        if (ms >= 0) {
            mp_float_t d = ms / MICROPY_FLOAT_CONST(1000.0) * SYNTH_RATE;
            if (f_left >= 10) {
                // Round to a whole number of left-channel cycles (MMBasic):
                // whole cycles as an integer, scaled back by the FLOAT period.
                mp_float_t hw = (mp_float_t)SYNTH_RATE / f_left; // samples per cycle
                duration = (uint64_t)((mp_float_t)(uint64_t)(d / hw) * hw);
            } else {
                duration = (uint64_t)d;
            }
        }
    }
    tone_mono = (f_left == f_right);
    tone_phinc_l = f_left / SYNTH_RATE * 4096.0f;
    tone_phinc_r = f_right / SYNTH_RATE * 4096.0f;
    if (mp_obj_is_true(args[3])) {
        tone_phase_l = 0;
        tone_phase_r = 0;
    }
    tone_remaining = duration;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(audio_tone_start_obj, 4, 4, audio_tone_start);

// tone_read(buf) -> bytes of 16-bit stereo PCM written (0 = tone finished).
static mp_obj_t audio_tone_read(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    int16_t *samples = (int16_t *)bufinfo.buf;
    int max_samples = bufinfo.len / sizeof(int16_t);
    int n = 0;
    while (n + 1 < max_samples && tone_remaining > 0) {
        if (tone_remaining != TONE_FOREVER) {
            tone_remaining--;
        }
        int16_t l = (int16_t)((SineTable[(int)tone_phase_l] - 2000) * 16);
        int16_t r;
        tone_phase_l += tone_phinc_l;
        if (tone_phase_l >= 4096.0f) {
            tone_phase_l -= 4096.0f;
        }
        if (tone_mono) {
            r = l;
        } else {
            r = (int16_t)((SineTable[(int)tone_phase_r] - 2000) * 16);
            tone_phase_r += tone_phinc_r;
            if (tone_phase_r >= 4096.0f) {
                tone_phase_r -= 4096.0f;
            }
        }
        samples[n++] = l;
        samples[n++] = r;
    }
    return MP_OBJ_NEW_SMALL_INT(n * sizeof(int16_t));
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_tone_read_obj, audio_tone_read);

// --- 4-voice synthesiser (MMBasic PLAY SOUND / fillSoundBuffer) -------------
//
// MAXSOUNDS voices, each with an independent left and right side: waveform,
// frequency and volume (0..25, mapped through MMBasic's mapping[] table via
// index vol*41/25 so four voices at full volume just fill the 16-bit range).
// Volume changes RAMP at 1 step per ~1 ms (44 samples) to avoid clicks.

#define MAXSOUNDS 4
#define SOUND_RAMP_INTERVAL 44 // step volumes once per ~1ms at 44100Hz (MMBasic)

enum {
    SND_OFF = 0,   // "O"
    SND_SINE,      // "S"
    SND_SQUARE,    // "Q"
    SND_TRI,       // "T"
    SND_SAW,       // "W"
    SND_PNOISE,    // "P" periodic noise (random table scanned by phase)
    SND_WNOISE,    // "N" white noise (random level held for a freq-set dwell)
};

typedef struct {
    uint8_t type;
    float phase;     // 0..4096 (SND_WNOISE: unused)
    float phinc;     // per-sample phase increment (SND_WNOISE: dwell length)
    int vol;         // current mapping[] index 0..41 (ramped)
    int vol_target;  // ramp target
    int dwell;       // white noise: samples until a new random level
    int noiseval;    // white noise: current raw level (100..3900)
} snd_voice_t;

static snd_voice_t snd_voices[MAXSOUNDS][2]; // [voice][0=left 1=right]
static uint16_t *snd_noisetable;             // 4096 random 100..3900 (lazy, PSRAM)

// sound_reset(): all voices off and volumes zeroed (fresh playback start).
static mp_obj_t audio_sound_reset(void) {
    memset(snd_voices, 0, sizeof(snd_voices));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_sound_reset_obj, audio_sound_reset);

// sound_set(voice, left, right, type, freq, vol): configure one voice.
// voice 0..3; left/right bools select the side(s); type SND_*; freq 1..20000;
// vol 0..25 (MMBasic's per-voice limit: 100 / MAXSOUNDS).
static mp_obj_t audio_sound_set(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_int_t voice = mp_obj_get_int(args[0]);
    bool left = mp_obj_is_true(args[1]);
    bool right = mp_obj_is_true(args[2]);
    mp_int_t type = mp_obj_get_int(args[3]);
    mp_float_t freq = mp_obj_get_float(args[4]);
    mp_int_t vol = mp_obj_get_int(args[5]);
    if (voice < 0 || voice >= MAXSOUNDS || type < SND_OFF || type > SND_WNOISE) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid voice or type"));
    }
    if (freq < 1 || freq > 20000) {
        mp_raise_ValueError(MP_ERROR_TEXT("valid is 1Hz to 20KHz"));
    }
    if (vol < 0 || vol > 25) {
        mp_raise_ValueError(MP_ERROR_TEXT("volume must be 0..25"));
    }
    if (type == SND_PNOISE && snd_noisetable == NULL) { // MMBasic setnoise()
        snd_noisetable = audio_cb_malloc(4096 * sizeof(uint16_t), NULL);
        if (snd_noisetable == NULL) {
            mp_raise_type(&mp_type_MemoryError);
        }
        for (int i = 0; i < 4096; i++) {
            snd_noisetable[i] = rand() % 3800 + 100;
        }
    }
    // White noise interprets the frequency as the dwell length in samples;
    // everything else as a SineTable phase increment (MMBasic getsound).
    float phinc = (type == SND_WNOISE) ? (float)freq : (float)(freq / SYNTH_RATE * 4096.0);
    for (int side = 0; side < 2; side++) {
        if (!(side == 0 ? left : right)) {
            continue;
        }
        snd_voice_t *v = &snd_voices[voice][side];
        if (v->type != type) {
            v->phase = 0; // new waveform starts at zero phase (MMBasic)
            v->dwell = 0;
        }
        v->phinc = phinc;
        v->type = type;
        v->vol_target = vol * 41 / 25; // mapping[] index, as MMBasic
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(audio_sound_set_obj, 6, 6, audio_sound_set);

// polyBLEP for the computed waveforms.  A square edge or sawtooth wrap that
// can only land on a sample tick carries alias images that beat against the
// true harmonics - an audible pitch-dependent shimmer on sustained notes.
// The band-limited edge differs from the naive one only within one tick of
// the step, where the residual is (1-t)^2 of the step toward its midpoint
// (t = distance from the edge in ticks).  The sine and triangle tables roll
// off fast enough to leave alone; both noises are broadband by design.
// Phase is 0..4096 with the wrap already applied, so an edge-adjacent sample
// shows up either just past the edge (distance < phinc) or just before it.
static inline int snd_blep_square(int j, float ph, float inc) {
    float d;
    if (inc <= 0.0f) {
        return j;
    }
    if (ph < inc) {
        d = ph;                 // just after the wrap edge
    } else if (4096.0f - ph < inc) {
        d = 4096.0f - ph;       // just before the wrap edge
    } else if (ph >= 2048.0f && ph - 2048.0f < inc) {
        d = ph - 2048.0f;       // just after the half-cycle edge
    } else if (ph < 2048.0f && 2048.0f - ph < inc) {
        d = 2048.0f - ph;       // just before the half-cycle edge
    } else {
        return j;
    }
    d = 1.0f - d / inc;         // 1 at the edge, 0 a tick away
    return 2000 + (int)((float)(j - 2000) * (1.0f - d * d));
}

static inline int snd_blep_saw(int j, float ph, float inc) {
    float d;
    if (inc <= 0.0f) {
        return j;
    }
    if (ph < inc) {             // just after the wrap: lift toward the midpoint
        d = 1.0f - ph / inc;
        return j + (int)(1900.0f * d * d);
    }
    if (4096.0f - ph < inc) {   // just before the wrap: pull toward the midpoint
        d = 1.0f - (4096.0f - ph) / inc;
        return j - (int)(1900.0f * d * d);
    }
    return j;
}

// One side of one voice -> raw table value 100..3900 (MMBasic getsound), then
// scaled by the ramped volume to roughly +/-480 per voice at full volume.
static inline int snd_sample(snd_voice_t *v) {
    int j;
    switch (v->type) {
        case SND_SINE:
            j = SineTable[(int)v->phase];
            break;
        case SND_TRI:
            j = triangletable[(int)v->phase];
            break;
        case SND_SQUARE:
            j = (v->phase > 2047.0f) ? 3900 : 100;
            j = snd_blep_square(j, v->phase, v->phinc);
            break;
        case SND_SAW:
            j = (int)v->phase * 3800 / 4096 + 100;
            j = snd_blep_saw(j, v->phase, v->phinc);
            break;
        case SND_PNOISE:
            j = snd_noisetable[(int)v->phase];
            break;
        case SND_WNOISE:
            if (v->dwell <= 0) {
                v->dwell = (int)v->phinc;
                v->noiseval = rand() % 3800 + 100;
            }
            if (v->dwell) {
                v->dwell--;
            }
            return (v->noiseval - 2000) * mapping[v->vol] / 2000;
        default:
            return 0;
    }
    v->phase += v->phinc;
    if (v->phase >= 4096.0f) {
        v->phase -= 4096.0f;
    }
    return (j - 2000) * mapping[v->vol] / 2000;
}

// sound_read(buf) -> always fills buf with 16-bit stereo PCM (the synth never
// ends; stop by stopping the pump). MMBasic fillSoundBuffer, I2S branch.
static mp_obj_t audio_sound_read(mp_obj_t buf_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    int16_t *samples = (int16_t *)bufinfo.buf;
    int max_samples = bufinfo.len / sizeof(int16_t);
    int n = 0;
    int ramp_counter = 0;
    while (n + 1 < max_samples) {
        if (++ramp_counter >= SOUND_RAMP_INTERVAL) {
            ramp_counter = 0;
            for (int i = 0; i < MAXSOUNDS; i++) {
                for (int side = 0; side < 2; side++) {
                    snd_voice_t *v = &snd_voices[i][side];
                    if (v->vol < v->vol_target) {
                        v->vol++;
                    } else if (v->vol > v->vol_target) {
                        v->vol--;
                    }
                }
            }
        }
        int leftv = 0, rightv = 0;
        for (int i = 0; i < MAXSOUNDS; i++) {
            if (snd_voices[i][0].type != SND_OFF) {
                leftv += snd_sample(&snd_voices[i][0]);
            }
            if (snd_voices[i][1].type != SND_OFF) {
                rightv += snd_sample(&snd_voices[i][1]);
            }
        }
        samples[n++] = (int16_t)(leftv * 16);
        samples[n++] = (int16_t)(rightv * 16);
    }
    return MP_OBJ_NEW_SMALL_INT(n * sizeof(int16_t));
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_sound_read_obj, audio_sound_read);

// --- MOD tracker playback (MMBasic PLAY MODFILE / MODSAMPLE, via hxcmod) ----
//
// The whole .mod file is loaded into a Python bytes object (PSRAM heap); the
// Python side keeps it referenced for the duration of playback because hxcmod
// plays the sample data in place. The modcontext is allocated through the
// rooted audio allocator so the GC can't reclaim it while the pump runs.
// Output is 16-bit stereo at MOD_RATE (MMBasic's modfilesamplerate; MMBasic
// outputs each sample twice at 44100 — we simply run I2S at 22050).

#define MOD_RATE 22050

static modcontext *g_mod = NULL;
static int g_mod_noloop, g_mod_ended;

// mod_open(data, loop) -> song title. data = the entire .mod file contents.
static mp_obj_t audio_mod_open(mp_obj_t data_in, mp_obj_t loop_in) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data_in, &bufinfo, MP_BUFFER_READ);
    if (g_mod != NULL) {
        audio_cb_free(g_mod, NULL);
        g_mod = NULL;
    }
    modcontext *ctx = audio_cb_malloc(sizeof(modcontext), NULL);
    if (ctx == NULL) {
        mp_raise_type(&mp_type_MemoryError);
    }
    hxcmod_init(ctx);
    hxcmod_setcfg(ctx, MOD_RATE, 1, 1); // rate, stereo separation, filter (MMBasic)
    if (!hxcmod_load(ctx, bufinfo.buf, bufinfo.len) || !ctx->mod_loaded) {
        audio_cb_free(ctx, NULL);
        mp_raise_ValueError(MP_ERROR_TEXT("not a valid MOD file"));
    }
    g_mod = ctx;
    g_mod_noloop = !mp_obj_is_true(loop_in);
    g_mod_ended = 0;
    // Song title: up to 20 bytes, not guaranteed NUL-terminated.
    char title[21];
    memcpy(title, ctx->song.title, 20);
    title[20] = '\0';
    return mp_obj_new_str(title, strlen(title));
}
static MP_DEFINE_CONST_FUN_OBJ_2(audio_mod_open_obj, audio_mod_open);

// mod_read(buf) -> bytes of 16-bit stereo PCM at MOD_RATE (0 = song finished).
// The buffer is cleared first: at song end (noloop) hxcmod returns mid-fill,
// so the tail would otherwise repeat stale samples (MMBasic plays them).
static mp_obj_t audio_mod_read(mp_obj_t buf_in) {
    if (g_mod == NULL || g_mod_ended) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_WRITE);
    memset(bufinfo.buf, 0, bufinfo.len);
    if (hxcmod_fillbuffer(g_mod, (msample *)bufinfo.buf, bufinfo.len / 4, NULL, g_mod_noloop)) {
        g_mod_ended = 1; // this buffer holds the final samples; next read = 0
    }
    return mp_obj_new_int((mp_int_t)bufinfo.len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(audio_mod_read_obj, audio_mod_read);

static mp_obj_t audio_mod_close(void) {
    if (g_mod != NULL) {
        audio_cb_free(g_mod, NULL);
        g_mod = NULL;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(audio_mod_close_obj, audio_mod_close);

// mod_sample(sample, effect, vol, rate): play one of the MOD's instrument
// samples as a sound effect MIXED OVER the running song (MMBasic MODSAMPLE,
// via MMBasic's hxcmod_playsoundeffect extension). sample 1..32, effect
// channel 1..4, vol 1..64, rate in Hz (MMBasic uses 16000).
static mp_obj_t audio_mod_sample(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    if (g_mod == NULL || g_mod_ended) {
        mp_raise_ValueError(MP_ERROR_TEXT("no MOD file playing"));
    }
    mp_int_t sample = mp_obj_get_int(args[0]);
    mp_int_t effect = mp_obj_get_int(args[1]);
    mp_int_t vol = mp_obj_get_int(args[2]);
    mp_int_t rate = mp_obj_get_int(args[3]);
    if (sample < 1 || sample > 32 || effect < 1 || effect > NUMMAXSEFFECTS) {
        mp_raise_ValueError(MP_ERROR_TEXT("sample must be 1..32, effect 1..4"));
    }
    if (vol < 1 || vol > 64) {
        mp_raise_ValueError(MP_ERROR_TEXT("volume must be 1..64"));
    }
    if (rate < 2000 || rate > 44100) {
        mp_raise_ValueError(MP_ERROR_TEXT("rate must be 2000..44100"));
    }
    unsigned int period = 3579545 / rate; // Amiga period (MMBasic)
    hxcmod_playsoundeffect(g_mod, sample - 1, effect - 1, vol - 1, period);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(audio_mod_sample_obj, 4, 4, audio_mod_sample);

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
    // Tone generator + 4-voice synth (MMBasic PLAY TONE / PLAY SOUND).
    { MP_ROM_QSTR(MP_QSTR_tone_start), MP_ROM_PTR(&audio_tone_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_tone_read), MP_ROM_PTR(&audio_tone_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound_reset), MP_ROM_PTR(&audio_sound_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound_set), MP_ROM_PTR(&audio_sound_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound_read), MP_ROM_PTR(&audio_sound_read_obj) },
    // MOD tracker (MMBasic PLAY MODFILE / PLAY MODSAMPLE).
    { MP_ROM_QSTR(MP_QSTR_mod_open), MP_ROM_PTR(&audio_mod_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_mod_read), MP_ROM_PTR(&audio_mod_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_mod_close), MP_ROM_PTR(&audio_mod_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_mod_sample), MP_ROM_PTR(&audio_mod_sample_obj) },
    // Waveform ids for sound_set (pcaudio maps MMBasic's letters to these).
    { MP_ROM_QSTR(MP_QSTR_OFF), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_SINE), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_SQUARE), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_TRIANGLE), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_SAW), MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_PNOISE), MP_ROM_INT(5) },
    { MP_ROM_QSTR(MP_QSTR_WNOISE), MP_ROM_INT(6) },
};
static MP_DEFINE_CONST_DICT(audio_module_globals, audio_module_globals_table);

const mp_obj_module_t audio_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&audio_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_audio, audio_module);

// Live decoder allocations, kept reachable by the GC (audio_cb_malloc/free).
MP_REGISTER_ROOT_POINTER(void *audio_allocs[16]);
