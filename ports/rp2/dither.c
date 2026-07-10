/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)  Copyright (c) 2026 Pico Computer 3
 */

// RGB121 error-diffusion dithering. Ported verbatim from MMBasic FileIO.c
// (rgb888_to_rgb121_dither / unpack_rgb121 / dither_image_row); restructured to a
// small stateful per-row API and to emit palette indices for our packed 4bpp
// framebuffer. See dither.h.

#include <string.h>
#include "py/runtime.h"
#include "dither.h"

// RGB888 -> RGB121 index (bit3=R, bits2:1=G, bit0=B), on the fixed grid.
static uint8_t rgb888_to_rgb121_dither(int16_t r, int16_t g, int16_t b) {
    r = (r < 0) ? 0 : (r > 255) ? 255 : r;
    g = (g < 0) ? 0 : (g > 255) ? 255 : g;
    b = (b < 0) ? 0 : (b > 255) ? 255 : b;
    uint8_t r1 = (r >= 128) ? 1 : 0;
    uint8_t g2 = (g * 3 + 127) / 255;
    uint8_t b1 = (b >= 128) ? 1 : 0;
    return (r1 << 3) | (g2 << 1) | b1;
}

// RGB121 index -> the grid RGB888 it represents (for the error calculation).
static void unpack_rgb121(uint8_t packed, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r1 = (packed >> 3) & 1;
    uint8_t g2 = (packed >> 1) & 3;
    uint8_t b1 = packed & 1;
    *r = r1 ? 255 : 0;
    *g = (uint8_t)((g2 * 255) / 3);
    *b = b1 ? 255 : 0;
}

// RGB888 -> RGB332 byte (RRRGGGBB), on the fixed grid.
static uint8_t rgb888_to_rgb332_dither(int16_t r, int16_t g, int16_t b) {
    r = (r < 0) ? 0 : (r > 255) ? 255 : r;
    g = (g < 0) ? 0 : (g > 255) ? 255 : g;
    b = (b < 0) ? 0 : (b > 255) ? 255 : b;
    uint8_t r3 = (r * 7 + 127) / 255;
    uint8_t g3 = (g * 7 + 127) / 255;
    uint8_t b2 = (b * 3 + 127) / 255;
    return (r3 << 5) | (g3 << 2) | b2;
}

static void unpack_rgb332(uint8_t packed, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r3 = (packed >> 5) & 7;
    uint8_t g3 = (packed >> 2) & 7;
    uint8_t b2 = packed & 3;
    *r = (uint8_t)((r3 * 255) / 7);
    *g = (uint8_t)((g3 * 255) / 7);
    *b = (uint8_t)((b2 * 255) / 3);
}

static inline uint8_t dither_quantize(int fmt, int16_t r, int16_t g, int16_t b) {
    return (fmt == DITHER_FMT_RGB332) ? rgb888_to_rgb332_dither(r, g, b)
                                      : rgb888_to_rgb121_dither(r, g, b);
}

static inline void dither_unpack(int fmt, uint8_t p, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (fmt == DITHER_FMT_RGB332) {
        unpack_rgb332(p, r, g, b);
    } else {
        unpack_rgb121(p, r, g, b);
    }
}

bool dither_init(dither_t *d, int method, int format, int width) {
    d->method = method;
    d->format = format;
    d->width = width;
    d->curr = NULL;
    d->next = NULL;
    if (method == DITHER_NONE || width <= 0) {
        return false;
    }
    size_t n = (size_t)width * 3;
    d->curr = m_malloc_maybe(n * sizeof(int16_t));
    d->next = m_malloc_maybe(n * sizeof(int16_t));
    if (d->curr == NULL || d->next == NULL) {
        dither_free(d);
        return false;
    }
    memset(d->curr, 0, n * sizeof(int16_t));
    memset(d->next, 0, n * sizeof(int16_t));
    return true;
}

void dither_free(dither_t *d) {
    if (d->curr != NULL) {
        m_free(d->curr);
        d->curr = NULL;
    }
    if (d->next != NULL) {
        m_free(d->next);
        d->next = NULL;
    }
}

void dither_row(dither_t *d, const uint8_t *rgb, uint8_t *out) {
    const int w = d->width;
    const int fmt = d->format;
    int16_t *curr = d->curr;
    int16_t *next = d->next;
    const int is_fs = (d->method == DITHER_FS);

    for (int x = 0; x < w; x++) {
        int16_t old_r = rgb[x * 3 + 0] + curr[x * 3 + 0];
        int16_t old_g = rgb[x * 3 + 1] + curr[x * 3 + 1];
        int16_t old_b = rgb[x * 3 + 2] + curr[x * 3 + 2];

        uint8_t packed = dither_quantize(fmt, old_r, old_g, old_b);
        out[x] = packed;

        uint8_t nr, ng, nb;
        dither_unpack(fmt, packed, &nr, &ng, &nb);

        if (old_r < 0) old_r = 0; else if (old_r > 255) old_r = 255;
        if (old_g < 0) old_g = 0; else if (old_g > 255) old_g = 255;
        if (old_b < 0) old_b = 0; else if (old_b > 255) old_b = 255;

        int16_t er = old_r - nr;
        int16_t eg = old_g - ng;
        int16_t eb = old_b - nb;

        if (is_fs) {
            // Floyd-Steinberg: 7/16 right, 3/16 down-left, 5/16 down, 1/16 down-right.
            if (x + 1 < w) {
                curr[(x + 1) * 3 + 0] += er * 7 / 16;
                curr[(x + 1) * 3 + 1] += eg * 7 / 16;
                curr[(x + 1) * 3 + 2] += eb * 7 / 16;
            }
            if (x > 0) {
                next[(x - 1) * 3 + 0] += er * 3 / 16;
                next[(x - 1) * 3 + 1] += eg * 3 / 16;
                next[(x - 1) * 3 + 2] += eb * 3 / 16;
            }
            next[x * 3 + 0] += er * 5 / 16;
            next[x * 3 + 1] += eg * 5 / 16;
            next[x * 3 + 2] += eb * 5 / 16;
            if (x + 1 < w) {
                next[(x + 1) * 3 + 0] += er * 1 / 16;
                next[(x + 1) * 3 + 1] += eg * 1 / 16;
                next[(x + 1) * 3 + 2] += eb * 1 / 16;
            }
        } else {
            // Atkinson: 1/8 to each of six neighbours (drops 2/8 of the error).
            if (x + 1 < w) {
                curr[(x + 1) * 3 + 0] += er / 8;
                curr[(x + 1) * 3 + 1] += eg / 8;
                curr[(x + 1) * 3 + 2] += eb / 8;
            }
            if (x + 2 < w) {
                curr[(x + 2) * 3 + 0] += er / 8;
                curr[(x + 2) * 3 + 1] += eg / 8;
                curr[(x + 2) * 3 + 2] += eb / 8;
            }
            if (x > 0) {
                next[(x - 1) * 3 + 0] += er / 8;
                next[(x - 1) * 3 + 1] += eg / 8;
                next[(x - 1) * 3 + 2] += eb / 8;
            }
            next[x * 3 + 0] += er / 8;
            next[x * 3 + 1] += eg / 8;
            next[x * 3 + 2] += eb / 8;
            if (x + 1 < w) {
                next[(x + 1) * 3 + 0] += er / 8;
                next[(x + 1) * 3 + 1] += eg / 8;
                next[(x + 1) * 3 + 2] += eb / 8;
            }
        }
    }

    // The next row's incoming errors are now in `next`; make it current and clear
    // a fresh accumulator.
    d->curr = next;
    d->next = curr;
    memset(d->next, 0, (size_t)w * 3 * sizeof(int16_t));
}
