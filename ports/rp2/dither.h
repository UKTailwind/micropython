/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)  Copyright (c) 2026 Pico Computer 3
 */

// Error-diffusion dithering for the image loaders, to either the RGB121 16-colour
// grid (4bpp framebuffer) or the RGB332 grid (8bpp / RGB640). Quantisers +
// Floyd-Steinberg / Atkinson distribution ported verbatim from MMBasic FileIO.c.
// RGB121 quantises to the fixed bit-expansion (index bit3=R, bits2:1=G, bit0=B) =
// the RGB121 default palette, so a heavily customised palette may not match.

#ifndef MICROPY_INCLUDED_RP2_DITHER_H
#define MICROPY_INCLUDED_RP2_DITHER_H

#include <stdint.h>
#include <stdbool.h>

#define DITHER_NONE     (0)
#define DITHER_FS       (1) // Floyd-Steinberg
#define DITHER_ATKINSON (2)

#define DITHER_FMT_RGB121 (0) // out value = 4-bit palette index
#define DITHER_FMT_RGB332 (1) // out value = 8-bit RGB332 byte

typedef struct {
    int method;
    int format;      // DITHER_FMT_*
    int width;
    int16_t *curr;   // width*3 error accumulators for the current row
    int16_t *next;   // width*3 for the next row
} dither_t;

// Allocate + zero the two error rows (GC heap / PSRAM). Returns false and leaves
// the struct empty if method == DITHER_NONE, width <= 0, or on OOM (the caller
// then just falls back to the plain nearest-colour path).
bool dither_init(dither_t *d, int method, int format, int width);
void dither_free(dither_t *d);

// Dither one image row: `rgb` = `width` RGB triplets (r,g,b bytes). Writes `width`
// quantised values to `out` (a 4-bit RGB121 index or an 8-bit RGB332 byte, per the
// format), advancing the error state so the next call diffuses into the following
// row. Rows MUST be supplied top-to-bottom.
void dither_row(dither_t *d, const uint8_t *rgb, uint8_t *out);

#endif // MICROPY_INCLUDED_RP2_DITHER_H
