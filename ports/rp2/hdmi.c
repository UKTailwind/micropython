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

// HDMI (HSTX DVI) driver for the Pico Computer 3. Two modes, selected at init():
//
//   RGB565  - 320x240 RGB565, pixel-doubled H (core1 fill-loop) + line-doubled V
//             (DMA reads source line = active/2). 153 KB. 65536 colours.
//   RGB332  - 640x480 RGB332 (8bpp), NATIVE resolution and native HSTX format, so
//             the DMA scans the framebuffer directly (no doubling, no fill-loop).
//             307 KB. 256 colours, crisp 640x480 (80x60 text with an 8x8 font).
//
// Both output a 640x480@60 DVI signal. Register technique/values adapted from the
// MMBasic PicoMite graphics/HDMI.c. Scanout runs entirely on core1 with a
// RAM-resident DMA IRQ (line 1) and SRAM framebuffer/line buffers; core1 is
// launched with its own stack and is NOT a multicore lockout victim, so core0
// flash writes never pause it. _thread is disabled so nothing else takes core1.

#include <string.h>

#include "py/runtime.h"
#include "py/objarray.h" // mp_obj_new_memoryview (hdmi.framebuffer())

#if MICROPY_HW_ENABLE_HDMI

#include "hdmi_priv.h"

// MMBasic font set. Font 1 (8x12) is the console font; hdmi_fonts[] holds all
// nine (see fonts.h for the packed-bitstream glyph format).
#include "fonts.h"
#define FONT_W     (8)
#define FONT_H     (12)
#define FONT_FIRST (0x20)



// --- State ------------------------------------------------------------------
uint8_t hdmi_fb[HDMI_FB_BYTES] __attribute__((aligned(4)));
volatile bool hdmi_running = false;

int hdmi_mode = HDMI_MODE_RGB640;
int hdmi_w = 640, hdmi_h = 480;    // logical framebuffer dimensions
int hdmi_transfer_count = 160;     // active pixel words per line
int hdmi_native = 1;               // 1 = native 8bpp scan (RGB640); 0 = doubled RGB565
int hdmi_rgb121 = 0;               // 1 = 4bpp packed framebuffer, core1 expands to an RGB332 line buffer

// RGB121 16-colour palette (RGB888, 0xRRGGBB). Default = MMBasic's MAP16DEF: a
// pure RGB121 bit-expansion, so an index's bit3=R, bits2:1=G, bit0=B map straight
// to the colour. User-settable at runtime via hdmi.palette().
static const uint32_t hdmi_pal_default[16] = {
    0x000000, 0x0000FF, 0x005500, 0x0055FF, 0x00AA00, 0x00AAFF, 0x00FF00, 0x00FFFF,
    0xFF0000, 0xFF00FF, 0xFF5500, 0xFF55FF, 0xFFAA00, 0xFFAAFF, 0xFFFF00, 0xFFFFFF,
};
static uint32_t hdmi_pal888[16];    // live palette (SRAM); lazily loaded from default
static bool hdmi_pal_ready = false;
// SRAM expansion table for RGB121 scanout: source byte (two 4-bit pixels) -> the
// two RGB332 pixels packed little-endian (low byte = even/left pixel). Rebuilt
// from hdmi_pal888 at init and on any palette change. MUST be SRAM (not flash):
// core1's fill loop reads it per pixel, and this board disables the multicore
// flash lockout, so a core0 flash write (e.g. saving settings) would otherwise
// starve or fault the scanout while XIP is down.
uint16_t hdmi_map256[256];

// Load the default palette the first time it's needed (so a palette set before
// any RGB121 init still starts from the defaults, not zeroed RAM).
static void hdmi_pal_ensure(void) {
    if (!hdmi_pal_ready) {
        memcpy(hdmi_pal888, hdmi_pal_default, sizeof(hdmi_pal888));
        hdmi_pal_ready = true;
    }
}

// Rebuild the byte->2px RGB332 expansion table from the live RGB888 palette. Safe
// to call while core1 is scanning: every entry is always a valid RGB332 pair, so
// a concurrent read sees at worst a one-frame mix of old/new colours.
static void hdmi_pal_rebuild(void) {
    uint8_t p332[16];
    for (int i = 0; i < 16; i++) {
        uint32_t c = hdmi_pal888[i];
        p332[i] = (uint8_t)(((c >> 16) & 0xE0) | (((c >> 8) & 0xE0) >> 3) | ((c & 0xC0) >> 6));
    }
    for (int b = 0; b < 256; b++) {
        hdmi_map256[b] = (uint16_t)(p332[b >> 4] | (p332[b & 0x0f] << 8));
    }
}

// Nearest RGB121 palette index (0..15) for an RGB888 colour, by squared distance
// over the live palette. For the image loaders writing 4bpp packed framebuffers.
int hdmi_nearest_index(int r, int g, int b) {
    hdmi_pal_ensure();
    int best = 0;
    long best_d = 0x7fffffffL;
    for (int i = 0; i < 16; i++) {
        uint32_t c = hdmi_pal888[i];
        int dr = r - (int)((c >> 16) & 0xFF);
        int dg = g - (int)((c >> 8) & 0xFF);
        int db = b - (int)(c & 0xFF);
        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
            if (d == 0) {
                break;
            }
        }
    }
    return best;
}

// RGB888 for a palette index (for reading a 4bpp framebuffer back, e.g. BMP save).
uint32_t hdmi_index_rgb888(int i) {
    hdmi_pal_ensure();
    return hdmi_pal888[i & 0x0f];
}
uint32_t hdmi_clock_khz = 252000;  // clk_sys for this init (252/315/378)
static uint32_t hdmi_gen = 0;             // bumped each init() so the console resyncs

// --- Write target / layer / off-screen buffer (MMBasic FRAMEBUFFER) --------
//
// Three drawing targets, MMBasic's N / L / F:
//   N - the normal display framebuffer (hdmi_fb), always available.
//   L - the LAYER: RGB320 only. Lives in the SECOND HALF of the static video
//       memory (320x240x2 = 153,600 bytes each, exactly filling the 307,200-
//       byte buffer). When enabled, core1's fill loop merges it over the main
//       display per pixel: the layer pixel wins unless it equals the single
//       transparent colour (MMBasic's HDMI layer merge). Must be SRAM - core1
//       cannot scan PSRAM (same rule as the framebuffer itself).
//   F - an off-screen buffer in PSRAM (GC heap), display-sized, never scanned:
//       a drawing target and copy source/destination only (MMBasic
//       FRAMEBUFFER CREATE).
// hdmi.write("N"/"L"/"F") selects where ALL drawing goes: framebuffer()/fb(),
// fill/scroll/putc/text and (because pcimage passes hdmi.framebuffer()) the
// image loaders. hdmi.copy(src, dst) block-copies between any two targets.
#define HDMI_TARGET_N 0
#define HDMI_TARGET_L 1
#define HDMI_TARGET_F 2
static int hdmi_target = HDMI_TARGET_N;
volatile int hdmi_layer_on = 0;          // the backend merges the layer when set
volatile uint16_t hdmi_layer_transp = 0; // layer transparent colour (RGB565)

// Bytes in one mode-sized framebuffer (the drawable size, not the static array).
size_t hdmi_fb_bytes(void) {
    if (hdmi_rgb121) {
        return (size_t)hdmi_w * hdmi_h / 2;
    }
    if (hdmi_native) {
        return (size_t)hdmi_w * hdmi_h;
    }
    return (size_t)hdmi_w * hdmi_h * 2;
}

// Base address of a drawing target (NULL if that target doesn't exist now).
static uint8_t *hdmi_target_ptr(int target) {
    if (target == HDMI_TARGET_L) {
        return hdmi_layer_on ? hdmi_fb + hdmi_fb_bytes() : NULL;
    }
    if (target == HDMI_TARGET_F) {
        return MP_STATE_PORT(hdmi_framebuf_f);
    }
    return hdmi_fb;
}

// The current write target's base. If the selected target has vanished (e.g.
// a soft reset cleared the F buffer's root pointer), snap back to the display
// so hdmi.write() and the drawing functions stay consistent.
static uint8_t *hdmi_wbuf(void) {
    uint8_t *p = hdmi_target_ptr(hdmi_target);
    if (p == NULL) {
        hdmi_target = HDMI_TARGET_N;
        p = hdmi_fb;
    }
    return p;
}


// --- test pattern: 8 vertical colour bars (validates the lane mapping) -----
static void hdmi_fill_test_pattern(void) {
    static const uint16_t bars565[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000,
    };
    static const uint8_t bars332[8] = {
        0xFF, 0xFC, 0x1F, 0x1C, 0xE3, 0xE0, 0x03, 0x00,
    };
    if (hdmi_rgb121) {
        // 16 vertical bars, one per palette index (packed 2 px/byte).
        for (int y = 0; y < hdmi_h; y++) {
            uint8_t *row = &hdmi_fb[y * (hdmi_w / 2)];
            for (int i = 0; i < hdmi_w / 2; i++) {
                int x = i * 2;
                uint8_t lo = (uint8_t)((x * 16) / hdmi_w);
                uint8_t hi = (uint8_t)(((x + 1) * 16) / hdmi_w);
                row[i] = (uint8_t)((lo << 4) | (hi & 0x0f));
            }
        }
    } else if (hdmi_native) {
        for (int y = 0; y < hdmi_h; y++) {
            for (int x = 0; x < hdmi_w; x++) {
                hdmi_fb[y * hdmi_w + x] = bars332[(x * 8) / hdmi_w];
            }
        }
    } else {
        uint16_t *fb16 = (uint16_t *)hdmi_fb;
        for (int y = 0; y < hdmi_h; y++) {
            for (int x = 0; x < hdmi_w; x++) {
                fb16[y * hdmi_w + x] = bars565[(x * 8) / hdmi_w];
            }
        }
    }
}


// --- Python bindings -------------------------------------------------------
static mp_obj_t hdmi_init(size_t n_args, const mp_obj_t *args) {
    if (hdmi_running) {
        return mp_const_none;
    }
    int mode = (n_args > 0) ? mp_obj_get_int(args[0]) : HDMI_MODE_RGB640;
    if (mode != HDMI_MODE_RGB640 && mode != HDMI_MODE_RGB320 &&
        mode != HDMI_MODE_RGB512 && mode != HDMI_MODE_RGB1024 &&
        mode != HDMI_MODE_RGB640_4) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad mode"));
    }
    // Only 640x480 (RGB640) and 320x240 (RGB320) may vary the clock, and only to
    // one of the three validated speeds. RGB512 (1024x600) is fixed at 252 MHz
    // (clk_hstx = clk_sys = 252 MHz -> 50.4 MHz pixel); reject any other clock for
    // it rather than silently overriding, so a bad value can't be persisted.
    uint32_t clock = (n_args > 1) ? (uint32_t)mp_obj_get_int(args[1]) : 252;
    if (clock != 252 && clock != 315 && clock != 378) {
        mp_raise_ValueError(MP_ERROR_TEXT("clock must be 252, 315 or 378"));
    }
    if ((mode == HDMI_MODE_RGB512 || mode == HDMI_MODE_RGB1024) && clock != 252) {
        mp_raise_ValueError(MP_ERROR_TEXT("RGB512/RGB1024 only support 252 MHz"));
    }
    hdmi_clock_khz = clock * 1000;
    hdmi_backend_set_clock(hdmi_clock_khz); // before the scanout starts (rp2 moves clk_sys)
    hdmi_mode = mode;
    if (mode == HDMI_MODE_RGB640) {
        hdmi_w = 640;
        hdmi_h = 480;
        hdmi_native = 1;
        hdmi_rgb121 = 0;
        hdmi_transfer_count = MODE_H_ACTIVE_PIXELS / 4; // 4 px/word (8bpp native)
    } else if (mode == HDMI_MODE_RGB320) {
        hdmi_w = 320;
        hdmi_h = 240;
        hdmi_native = 0;
        hdmi_rgb121 = 0;
        hdmi_transfer_count = MODE_H_ACTIVE_PIXELS / 2; // 2 px/word (doubled 640-wide line)
    } else if (mode == HDMI_MODE_RGB512) {
        hdmi_w = 512;
        hdmi_h = 300;
        hdmi_native = 0;
        hdmi_rgb121 = 0;
        hdmi_transfer_count = X_H_ACTIVE_PIXELS / 2;   // 512 words (doubled 1024-wide line)
    } else if (mode == HDMI_MODE_RGB1024) {
        hdmi_w = 1024;
        hdmi_h = 600;
        hdmi_native = 0;
        hdmi_rgb121 = 1;
        hdmi_transfer_count = X_H_ACTIVE_PIXELS / 4;   // 256 words (native 1024-wide RGB332 line)
        hdmi_pal_ensure();    // load the default palette on first use
        hdmi_pal_rebuild();   // (re)build the SRAM expansion table from the live palette
    } else { // RGB640_4: 640x480 in 16 colours, core1-expanded like RGB1024
        hdmi_w = 640;
        hdmi_h = 480;
        hdmi_native = 0;
        hdmi_rgb121 = 1;
        hdmi_transfer_count = MODE_H_ACTIVE_PIXELS / 4; // 160 words (expanded 640-wide RGB332 line)
        hdmi_pal_ensure();
        hdmi_pal_rebuild();
    }
    // A mode change invalidates every FRAMEBUFFER-style target (buffer sizes
    // differ per mode): drop the layer, release the F buffer (the GC reclaims
    // it), and point drawing back at the display — as MMBasic's mode switch.
    hdmi_layer_on = 0;
    hdmi_target = HDMI_TARGET_N;
    MP_STATE_PORT(hdmi_framebuf_f) = NULL;
    memset(hdmi_fb, 0, sizeof(hdmi_fb)); // clear to black (0 = black in both formats)
    hdmi_gen++;                          // signal the console to resync/home
    hdmi_running = true;
    hdmi_backend_start(); // claim scanout resources and start refreshing
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_init_obj, 0, 2, hdmi_init);

// The CURRENT WRITE TARGET (display / layer / F buffer — see hdmi.write()) as a
// writable MEMORYVIEW alias, sized for the active mode. Use with
// framebuf.FrameBuffer(buf, hdmi.WIDTH, hdmi.HEIGHT, framebuf.RGB565 or GS8).
// A memoryview, not a bytearray: its repr is a few characters, so echoing
// hdmi.framebuffer() at the REPL is instant — a bytearray's repr is ~600 KB of
// hex spam through the UART + on-screen console (minutes of apparent lock-up,
// and a Ctrl-C landing inside the console's dupterm write deactivates it).
static mp_obj_t hdmi_framebuffer(void) {
    return mp_obj_new_memoryview('B' | MP_OBJ_ARRAY_TYPECODE_FLAG_RW,
        hdmi_fb_bytes(), hdmi_wbuf());
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_framebuffer_obj, hdmi_framebuffer);

// Ready-made pcgfx.Display (a framebuf.FrameBuffer subclass with RGB888
// colour() conversion) over the framebuffer at the current mode's geometry and
// pixel format (GS8 for RGB332, RGB565 otherwise). Rebuild after a mode change.
static mp_obj_t hdmi_make_fb(void) {
    mp_obj_t framebuf_mod = mp_import_name(MP_QSTR_framebuf, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t pcgfx_mod = mp_import_name(MP_QSTR_pcgfx, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    qstr fmt_q = hdmi_rgb121 ? MP_QSTR_GS4_HMSB : (hdmi_native ? MP_QSTR_GS8 : MP_QSTR_RGB565);
    mp_obj_t args[4] = {
        hdmi_framebuffer(),
        MP_OBJ_NEW_SMALL_INT(hdmi_w),
        MP_OBJ_NEW_SMALL_INT(hdmi_h),
        mp_load_attr(framebuf_mod, fmt_q),
    };
    return mp_call_function_n_kw(mp_load_attr(pcgfx_mod, MP_QSTR_Display), 4, 0, args);
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_fb_obj, hdmi_make_fb);

static mp_obj_t hdmi_stack_ok(void) {
    return mp_obj_new_bool(hdmi_backend_stack_ok());
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_stack_ok_obj, hdmi_stack_ok);

// Stop the scanout so a new mode can be started with init(). Halts core1, aborts
// the chained DMA cleanly, and stops HSTX. The claimed DMA channels are kept.
static mp_obj_t hdmi_deinit(void) {
    if (!hdmi_running) {
        return mp_const_none;
    }
    hdmi_running = false;
    hdmi_backend_stop(); // halt scanout, release the display hardware
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_deinit_obj, hdmi_deinit);

static mp_obj_t hdmi_fill(mp_obj_t colour_in) {
    mp_int_t col = mp_obj_get_int(colour_in);
    uint8_t *buf = hdmi_wbuf();
    if (hdmi_rgb121) {
        uint8_t c = (uint8_t)(col & 0x0f);
        memset(buf, (uint8_t)((c << 4) | c), (size_t)hdmi_w * hdmi_h / 2);
    } else if (hdmi_native) {
        uint8_t c = (uint8_t)col;
        for (int i = 0; i < hdmi_w * hdmi_h; i++) {
            buf[i] = c;
        }
    } else {
        uint16_t *fb16 = (uint16_t *)buf;
        uint16_t c = (uint16_t)col;
        for (int i = 0; i < hdmi_w * hdmi_h; i++) {
            fb16[i] = c;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(hdmi_fill_obj, hdmi_fill);

// Fast vertical scroll: move the framebuffer up by `rows` pixel rows (content
// moves up) and fill the exposed bottom `rows` rows with `colour`. A bulk
// memmove/memset, ~1000x faster than framebuf.scroll()'s per-pixel loop.
//
// hdmi.scroll(dy, colour=0, y0=0, height=None): scroll only the pixel band
// [y0, y0+height) (default: the whole screen). dy > 0 moves the band's content
// UP by dy pixels (blank at the band bottom); dy < 0 moves it DOWN (blank at
// the top). The band form drives the on-screen console's scroll region, so
// the editor (pye) can scroll its text area while leaving the status line put.

// Fill `nrows` pixel rows at `dst` with `colour` in the current pixel format.
static void hdmi_fill_rows(uint8_t *dst, int nrows, int stride, mp_int_t colour) {
    if (hdmi_rgb121) {
        uint8_t c = (uint8_t)(colour & 0x0f);
        memset(dst, (uint8_t)((c << 4) | c), (size_t)nrows * stride);
    } else if (hdmi_native) {
        memset(dst, (int)(colour & 0xFF), (size_t)nrows * stride);
    } else {
        uint16_t *p = (uint16_t *)dst;
        int count = nrows * hdmi_w;
        uint16_t c = (uint16_t)colour;
        for (int i = 0; i < count; i++) {
            p[i] = c;
        }
    }
}

static mp_obj_t hdmi_scroll(size_t n_args, const mp_obj_t *args) {
    int dy = mp_obj_get_int(args[0]);
    mp_int_t colour = (n_args > 1) ? mp_obj_get_int(args[1]) : 0;
    int y0 = (n_args > 2) ? mp_obj_get_int(args[2]) : 0;
    int band = (n_args > 3) ? mp_obj_get_int(args[3]) : (hdmi_h - y0);
    // Clamp the band to the framebuffer.
    if (y0 < 0) {
        band += y0;
        y0 = 0;
    }
    if (y0 + band > hdmi_h) {
        band = hdmi_h - y0;
    }
    if (band <= 0 || dy == 0) {
        return mp_const_none;
    }
    int mag = dy < 0 ? -dy : dy;
    uint8_t *buf = hdmi_wbuf();
    int stride = hdmi_rgb121 ? (hdmi_w / 2) : (hdmi_w * ((hdmi_native) ? 1 : 2));
    uint8_t *base = buf + (size_t)y0 * stride;
    if (mag >= band) {
        hdmi_fill_rows(base, band, stride, colour); // whole band cleared
        return mp_const_none;
    }
    int keep = band - mag;
    if (dy > 0) {
        // Content up: rows [mag..band) -> [0..keep); blank the bottom `mag`.
        memmove(base, base + (size_t)mag * stride, (size_t)keep * stride);
        hdmi_fill_rows(base + (size_t)keep * stride, mag, stride, colour);
    } else {
        // Content down: rows [0..keep) -> [mag..band); blank the top `mag`.
        memmove(base + (size_t)mag * stride, base, (size_t)keep * stride);
        hdmi_fill_rows(base, mag, stride, colour);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_scroll_obj, 1, 4, hdmi_scroll);

// Blit one 8x12 glyph opaquely at pixel (px,py): fg where the bit is set, bg
// elsewhere (so it also clears the cell). Format-aware; writes hdmi_fb directly.
static mp_obj_t hdmi_putc(size_t n_args, const mp_obj_t *args) {
    int px = mp_obj_get_int(args[0]);
    int py = mp_obj_get_int(args[1]);
    int ch = mp_obj_get_int(args[2]);
    mp_int_t fg = mp_obj_get_int(args[3]);
    mp_int_t bg = mp_obj_get_int(args[4]);
    if (ch < FONT_FIRST || ch > 0xFF) {
        ch = FONT_FIRST;
    }
    const uint8_t *glyph = &font1[4 + (ch - FONT_FIRST) * FONT_H];
    uint8_t *buf = hdmi_wbuf();
    for (int row = 0; row < FONT_H; row++) {
        int y = py + row;
        if (y < 0 || y >= hdmi_h) {
            continue;
        }
        uint8_t bits = glyph[row];
        if (hdmi_rgb121) {
            uint8_t *line = buf + (size_t)y * (hdmi_w / 2);
            for (int col = 0; col < FONT_W; col++) {
                int x = px + col;
                if (x >= 0 && x < hdmi_w) {
                    uint8_t v = (bits & (0x80 >> col)) ? (uint8_t)(fg & 0x0f) : (uint8_t)(bg & 0x0f);
                    uint8_t *pb = &line[x >> 1];
                    *pb = (x & 1) ? ((*pb & 0xf0) | v) : ((*pb & 0x0f) | (uint8_t)(v << 4));
                }
            }
        } else if (hdmi_native) {
            uint8_t *line = buf + (size_t)y * hdmi_w;
            for (int col = 0; col < FONT_W; col++) {
                int x = px + col;
                if (x >= 0 && x < hdmi_w) {
                    line[x] = (bits & (0x80 >> col)) ? (uint8_t)fg : (uint8_t)bg;
                }
            }
        } else {
            uint16_t *line = (uint16_t *)buf + (size_t)y * hdmi_w;
            for (int col = 0; col < FONT_W; col++) {
                int x = px + col;
                if (x >= 0 && x < hdmi_w) {
                    line[x] = (bits & (0x80 >> col)) ? (uint16_t)fg : (uint16_t)bg;
                }
            }
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_putc_obj, 5, 5, hdmi_putc);

// Resolve a 1-based MMBasic font number (1..HDMI_NFONTS) to its glyph data and
// metrics. Out-of-range falls back to font 1. Returns a pointer to the glyphs
// (past the 4-byte header).
static const uint8_t *hdmi_font_lookup(int fontno, int *w, int *h, int *first, int *count) {
    if (fontno < 1 || fontno > HDMI_NFONTS) {
        fontno = 1;
    }
    const uint8_t *fp = hdmi_fonts[fontno - 1];
    *w = fp[0];
    *h = fp[1];
    *first = fp[2];
    *count = fp[3];
    return fp + 4;
}

// Blit one glyph at pixel (px,py) scaled by `scale` (each font pixel -> a
// scale x scale block), from a resolved font (gdata/fw/fh/first/count). Glyphs
// are a continuous MSB-first bitstream (bit N = row*fw + col). fg/bg are
// native-format colours; a negative `bg` means a transparent background (only
// the set pixels are drawn). Characters outside the font clear their cell to bg
// (opaque) or draw nothing (transparent). Format-aware; writes hdmi_fb directly,
// clipped to the framebuffer.
static void hdmi_blit_glyph(int px, int py, int ch, mp_int_t fg, mp_int_t bg, int scale,
                            const uint8_t *gdata, int fw, int fh, int first, int count) {
    const uint8_t *glyph =
        (ch >= first && ch < first + count) ? gdata + (ch - first) * ((fw * fh) / 8) : NULL;
    bool transparent = (bg < 0);
    uint8_t *buf = hdmi_wbuf();
    for (int row = 0; row < fh; row++) {
        for (int sy = 0; sy < scale; sy++) {
            int y = py + row * scale + sy;
            if (y < 0 || y >= hdmi_h) {
                continue;
            }
            for (int col = 0; col < fw; col++) {
                bool on = false;
                if (glyph) {
                    int n = row * fw + col;
                    on = (glyph[n >> 3] >> (7 - (n & 7))) & 1;
                }
                if (!on && transparent) {
                    continue;
                }
                mp_int_t c = on ? fg : bg;
                int x0 = px + col * scale;
                for (int sx = 0; sx < scale; sx++) {
                    int x = x0 + sx;
                    if (x < 0 || x >= hdmi_w) {
                        continue;
                    }
                    if (hdmi_rgb121) {
                        uint8_t *pb = &buf[(size_t)y * (hdmi_w / 2) + (x >> 1)];
                        uint8_t v = (uint8_t)(c & 0x0f);
                        *pb = (x & 1) ? ((*pb & 0xf0) | v) : ((*pb & 0x0f) | (uint8_t)(v << 4));
                    } else if (hdmi_native) {
                        buf[(size_t)y * hdmi_w + x] = (uint8_t)c;
                    } else {
                        ((uint16_t *)buf)[(size_t)y * hdmi_w + x] = (uint16_t)c;
                    }
                }
            }
        }
    }
}

// hdmi.text(s, x, y, fg[, bg=-1[, scale=1[, font=1]]]) -- draw a string in an
// MMBasic font at pixel (x,y). bg<0 (default) is transparent; scale enlarges the
// glyphs; font is a 1-based font number (1..9, see hdmi.fonts()). Returns the x
// pixel just past the string (so calls can be chained).
static mp_obj_t hdmi_text(size_t n_args, const mp_obj_t *args) {
    size_t len;
    const char *s = mp_obj_str_get_data(args[0], &len);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    mp_int_t fg = mp_obj_get_int(args[3]);
    mp_int_t bg = (n_args > 4) ? mp_obj_get_int(args[4]) : -1;
    int scale = (n_args > 5) ? mp_obj_get_int(args[5]) : 1;
    int fontno = (n_args > 6) ? mp_obj_get_int(args[6]) : 1;
    if (scale < 1) {
        scale = 1;
    }
    int fw, fh, first, count;
    const uint8_t *gdata = hdmi_font_lookup(fontno, &fw, &fh, &first, &count);
    int adv = fw * scale;
    for (size_t i = 0; i < len; i++) {
        hdmi_blit_glyph(x, y, (uint8_t)s[i], fg, bg, scale, gdata, fw, fh, first, count);
        x += adv;
    }
    return mp_obj_new_int(x);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_text_obj, 4, 7, hdmi_text);

// hdmi.fonts() -- list the available fonts as (number, width, height, first,
// count) tuples, 1-based numbers matching the `font` argument of hdmi.text().
static mp_obj_t hdmi_fonts_fn(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (int i = 0; i < HDMI_NFONTS; i++) {
        const uint8_t *fp = hdmi_fonts[i];
        mp_obj_t item[5] = {
            MP_OBJ_NEW_SMALL_INT(i + 1), MP_OBJ_NEW_SMALL_INT(fp[0]),
            MP_OBJ_NEW_SMALL_INT(fp[1]), MP_OBJ_NEW_SMALL_INT(fp[2]),
            MP_OBJ_NEW_SMALL_INT(fp[3]),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(5, item));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_fonts_obj, hdmi_fonts_fn);

// Monotonic init counter — the console watches this to detect a mode/clock
// switch (which clears the screen) and resync/home itself.
static mp_obj_t hdmi_gen_fn(void) {
    return MP_OBJ_NEW_SMALL_INT(hdmi_gen);
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_gen_obj, hdmi_gen_fn);

// True if the framebuffer is 16-bit RGB565 (RGB320/RGB512); False for the RGB332
// (RGB640) or 4bpp RGB121 (RGB1024) formats. Prefer hdmi.bpp() for a 3-way test.
static mp_obj_t hdmi_rgb565(void) {
    return mp_obj_new_bool(!hdmi_native && !hdmi_rgb121);
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_rgb565_obj, hdmi_rgb565);

// Bits per framebuffer pixel: 4 (RGB1024), 8 (RGB640), or 16 (RGB320/RGB512).
static mp_obj_t hdmi_bpp(void) {
    return MP_OBJ_NEW_SMALL_INT(hdmi_rgb121 ? 4 : (hdmi_native ? 8 : 16));
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_bpp_obj, hdmi_bpp);

// The RGB1024 mode's 16-colour RGB121 palette (only affects RGB1024 output):
//   hdmi.palette()          -> a 16-tuple of the current RGB888 entries
//   hdmi.palette(i)         -> entry i (0..15) as a 24-bit RGB888 int
//   hdmi.palette(i, 0xRRGGBB) -> set entry i; rebuilds the expansion table so the
//                               change shows immediately, even while scanning.
static mp_obj_t hdmi_palette(size_t n_args, const mp_obj_t *args) {
    hdmi_pal_ensure();
    if (n_args == 0) {
        mp_obj_t items[16];
        for (int i = 0; i < 16; i++) {
            items[i] = mp_obj_new_int_from_uint(hdmi_pal888[i]);
        }
        return mp_obj_new_tuple(16, items);
    }
    int i = mp_obj_get_int(args[0]);
    if (i < 0 || i > 15) {
        mp_raise_ValueError(MP_ERROR_TEXT("index must be 0..15"));
    }
    if (n_args == 1) {
        return mp_obj_new_int_from_uint(hdmi_pal888[i]);
    }
    hdmi_pal888[i] = (uint32_t)mp_obj_get_int(args[1]) & 0xFFFFFFu;
    hdmi_pal_rebuild();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_palette_obj, 0, 2, hdmi_palette);

// Draw the 8-bar colour test pattern (bring-up aid; init() now clears to black).
static mp_obj_t hdmi_test(void) {
    hdmi_fill_test_pattern();
    hdmi_gen++;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_test_obj, hdmi_test);

// Current logical framebuffer geometry, for C consumers outside this file
// (e.g. USB touch coordinate scaling). 0 before hdmi.init().
int hdmi_get_width(void) {
    return hdmi_w;
}
int hdmi_get_height(void) {
    return hdmi_h;
}

static mp_obj_t hdmi_width(void) {
    return MP_OBJ_NEW_SMALL_INT(hdmi_w);
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_width_obj, hdmi_width);
static mp_obj_t hdmi_height(void) {
    return MP_OBJ_NEW_SMALL_INT(hdmi_h);
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_height_obj, hdmi_height);

// hdmi.vsync() -- wait for the START of the next vertical blanking interval,
// then return. Draw immediately after it returns and the scanout beam won't
// catch the update mid-frame; calling it once per game-loop iteration also
// paces the loop to the refresh rate (60/75 Hz). While waiting it pumps the
// event hooks (USB HID polling, the audio feeder, Ctrl-C), so a vsync-paced
// loop doesn't starve input or sound.
static mp_obj_t hdmi_vsync(void) {
    if (!hdmi_running) {
        return mp_const_none;
    }
    // If we're already inside blanking, let it pass (wait for the active
    // region) so every call waits for a *fresh* blanking start — consistent
    // once-per-frame pacing even when called back-to-back.
    while (hdmi_running && hdmi_backend_in_blanking()) {
        mp_event_handle_nowait();
    }
    while (hdmi_running && !hdmi_backend_in_blanking()) {
        mp_event_handle_nowait();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_vsync_obj, hdmi_vsync);

// --- MMBasic FRAMEBUFFER commands: layer / create / write / copy / close ----

// Parse a target letter ("N"/"L"/"F", case-insensitive) to HDMI_TARGET_*.
static int hdmi_parse_target(mp_obj_t obj) {
    const char *s = mp_obj_str_get_str(obj);
    if ((s[0] == 'N' || s[0] == 'n') && s[1] == '\0') {
        return HDMI_TARGET_N;
    }
    if ((s[0] == 'L' || s[0] == 'l') && s[1] == '\0') {
        return HDMI_TARGET_L;
    }
    if ((s[0] == 'F' || s[0] == 'f') && s[1] == '\0') {
        return HDMI_TARGET_F;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("target must be 'N', 'L' or 'F'"));
}

// A target's base pointer, raising if it hasn't been created.
static uint8_t *hdmi_target_ptr_checked(int target) {
    uint8_t *p = hdmi_target_ptr(target);
    if (p == NULL) {
        if (target == HDMI_TARGET_L) {
            mp_raise_ValueError(MP_ERROR_TEXT("layer not created"));
        }
        mp_raise_ValueError(MP_ERROR_TEXT("framebuffer not created"));
    }
    return p;
}

// hdmi.layer(transparent=0x000000) -- enable the overlay layer (RGB320 only,
// MMBasic FRAMEBUFFER LAYER). The layer occupies the second half of the video
// memory and is cleared to the transparent colour (RGB888, converted with the
// same formula as pcgfx colour(), so fb.colour(c) values match the merge test).
// Anything drawn in a different colour overlays the main display.
static mp_obj_t hdmi_layer_fn(size_t n_args, const mp_obj_t *args) {
    if (!hdmi_running || hdmi_mode != HDMI_MODE_RGB320) {
        mp_raise_ValueError(MP_ERROR_TEXT("layer needs RGB320 mode"));
    }
    if (hdmi_layer_on) {
        mp_raise_ValueError(MP_ERROR_TEXT("layer already exists"));
    }
    uint32_t rgb = (n_args > 0) ? ((uint32_t)mp_obj_get_int(args[0]) & 0xFFFFFFu) : 0;
    uint16_t t565 = (uint16_t)((((rgb >> 16) & 0xF8) << 8)
        | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
    // Fill the layer with the transparent colour BEFORE enabling the merge, so
    // it appears atomically (an all-transparent layer is invisible).
    uint16_t *l = (uint16_t *)(hdmi_fb + hdmi_fb_bytes());
    for (int i = 0; i < hdmi_w * hdmi_h; i++) {
        l[i] = t565;
    }
    hdmi_layer_transp = t565;
    hdmi_layer_on = 1;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_layer_obj, 0, 1, hdmi_layer_fn);

// hdmi.transparent() -- the layer's transparent colour as a native RGB565
// value, or -1 when no layer is active (pcsprite uses it as its erase colour).
static mp_obj_t hdmi_transparent_fn(void) {
    return hdmi_layer_on ? mp_obj_new_int(hdmi_layer_transp) : MP_OBJ_NEW_SMALL_INT(-1);
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_transparent_obj, hdmi_transparent_fn);

// hdmi.create() -- allocate the off-screen F buffer, display-sized, in PSRAM
// (GC heap; rooted so it survives while unreferenced from Python). MMBasic
// FRAMEBUFFER CREATE. Freed by hdmi.close("F") or any mode change.
static mp_obj_t hdmi_create(void) {
    if (!hdmi_running) {
        mp_raise_ValueError(MP_ERROR_TEXT("display not initialised"));
    }
    if (MP_STATE_PORT(hdmi_framebuf_f) != NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("framebuffer already exists"));
    }
    uint8_t *p;
    if (hdmi_rgb121 && hdmi_fb_bytes() * 2 <= sizeof(hdmi_fb)) {
        // RGB640_4: the 150 KB framebuffer is half the video SRAM, so the F
        // buffer takes the OTHER half -- fast SRAM instead of the PSRAM heap,
        // exactly MMBasic's fast-game-mode layout (compose + copy never touch
        // PSRAM). (RGB320's second half belongs to the layer; RGB1024 fills
        // the whole array; both keep using the heap below.)
        p = hdmi_fb + hdmi_fb_bytes();
    } else {
        p = m_malloc(hdmi_fb_bytes()); // PSRAM heap; raises MemoryError if exhausted
    }
    memset(p, 0, hdmi_fb_bytes());
    MP_STATE_PORT(hdmi_framebuf_f) = p;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_create_obj, hdmi_create);

// hdmi.write("N"/"L"/"F") -- select where ALL drawing goes (MMBasic
// FRAMEBUFFER WRITE): framebuffer()/fb(), fill/scroll/putc/text, the console
// and the image loaders. hdmi.write() returns the current target letter.
static mp_obj_t hdmi_write(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        static const char letters[3] = { 'N', 'L', 'F' };
        return mp_obj_new_str(&letters[hdmi_target], 1);
    }
    int target = hdmi_parse_target(args[0]);
    hdmi_target_ptr_checked(target); // must exist to be written
    hdmi_target = target;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_write_obj, 0, 1, hdmi_write);

// hdmi.copy(src, dst) -- block-copy one whole buffer to another ("N"/"L"/"F",
// MMBasic FRAMEBUFFER COPY). Both must exist; same-to-same is a no-op.
static mp_obj_t hdmi_copy(mp_obj_t src_in, mp_obj_t dst_in) {
    uint8_t *s = hdmi_target_ptr_checked(hdmi_parse_target(src_in));
    uint8_t *d = hdmi_target_ptr_checked(hdmi_parse_target(dst_in));
    if (s != d) {
        memcpy(d, s, hdmi_fb_bytes());
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(hdmi_copy_obj, hdmi_copy);

// --- Blitter (MMBasic BLIT, generalised to any src/dst surface) ------------

// A blit surface: a pixel buffer in the current mode's format with its own
// geometry. Either a screen target ("N"/"L"/"F", geometry = the mode) or a
// user buffer passed as (buffer, w, h) — e.g. a sprite image in a bytearray.
typedef struct {
    uint8_t *ptr;
    int w, h;
} hdmi_surf_t;

// Bytes needed for a w x h surface in the current mode's pixel format.
static size_t hdmi_surf_bytes(int w, int h) {
    if (hdmi_rgb121) {
        return (size_t)(w / 2) * h;
    }
    return (size_t)w * h * (hdmi_native ? 1 : 2);
}

// Resolve a blit src/dst argument: None = current write target, a letter =
// that screen target, or a (buffer, w, h) tuple/list = a user surface.
static hdmi_surf_t hdmi_parse_surface(mp_obj_t obj) {
    hdmi_surf_t s;
    if (obj == mp_const_none) {
        s.ptr = hdmi_wbuf();
        s.w = hdmi_w;
        s.h = hdmi_h;
        return s;
    }
    if (mp_obj_is_str(obj)) {
        s.ptr = hdmi_target_ptr_checked(hdmi_parse_target(obj));
        s.w = hdmi_w;
        s.h = hdmi_h;
        return s;
    }
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(obj, &len, &items);
    if (len != 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("surface must be 'N'/'L'/'F' or (buffer, w, h)"));
    }
    s.w = mp_obj_get_int(items[1]);
    s.h = mp_obj_get_int(items[2]);
    if (s.w < 1 || s.h < 1 || (hdmi_rgb121 && (s.w & 1))) {
        // 4bpp rows are nibble-packed: an odd width would split a byte
        // across rows, so RGB1024 surfaces must be even-width.
        mp_raise_ValueError(MP_ERROR_TEXT("bad surface size"));
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(items[0], &bufinfo, MP_BUFFER_RW);
    if (bufinfo.len < hdmi_surf_bytes(s.w, s.h)) {
        mp_raise_ValueError(MP_ERROR_TEXT("buffer too small for w x h"));
    }
    s.ptr = bufinfo.buf;
    return s;
}

// Read/write one pixel of a mode-format surface of width `sw`. The opaque
// byte-addressable fast path below never uses these; they serve the
// 4bpp-packed and skip-colour paths (sprite-sized blits).
static inline mp_int_t hdmi_px_get(const uint8_t *b, int sw, int x, int y) {
    if (hdmi_rgb121) {
        uint8_t v = b[(size_t)y * (sw / 2) + (x >> 1)];
        return (x & 1) ? (v & 0x0f) : (v >> 4);
    }
    if (hdmi_native) {
        return b[(size_t)y * sw + x];
    }
    return ((const uint16_t *)b)[(size_t)y * sw + x];
}
static inline void hdmi_px_set(uint8_t *b, int sw, int x, int y, mp_int_t v) {
    if (hdmi_rgb121) {
        uint8_t *p = &b[(size_t)y * (sw / 2) + (x >> 1)];
        *p = (x & 1) ? ((*p & 0xf0) | (uint8_t)(v & 0x0f))
                     : ((*p & 0x0f) | (uint8_t)((v & 0x0f) << 4));
    } else if (hdmi_native) {
        b[(size_t)y * sw + x] = (uint8_t)v;
    } else {
        ((uint16_t *)b)[(size_t)y * sw + x] = (uint16_t)v;
    }
}

// Core blit: copy the (x,y,w,h) rect of source `sf` to (x1,y1) of dest `df`,
// clipping both rectangles in step (MMBasic BLIT), with an optional skip colour.
// Shared by hdmi.blit and hdmi.tilemap. Assumes the display is running.
static void hdmi_do_blit(hdmi_surf_t sf, hdmi_surf_t df, int x, int y, int w, int h,
                         int x1, int y1, mp_int_t skip) {
    if (w < 1 || h < 1) {
        return;
    }
    // Clip both rectangles in step (MMBasic cmd_blit, verbatim shape): a
    // negative source origin shifts the destination (and vice versa), then
    // both are clamped to their surface's geometry.
    if (x < 0) {
        x1 -= x;
        w += x;
        x = 0;
    }
    if (x1 < 0) {
        x -= x1;
        w += x1;
        x1 = 0;
    }
    if (y < 0) {
        y1 -= y;
        h += y;
        y = 0;
    }
    if (y1 < 0) {
        y -= y1;
        h += y1;
        y1 = 0;
    }
    if (x + w > sf.w) {
        w = sf.w - x;
    }
    if (x1 + w > df.w) {
        w = df.w - x1;
    }
    if (y + h > sf.h) {
        h = sf.h - y;
    }
    if (y1 + h > df.h) {
        h = df.h - y1;
    }
    if (w < 1 || h < 1 || x < 0 || x + w > sf.w || x1 < 0 || x1 + w > df.w
        || y < 0 || y + h > sf.h || y1 < 0 || y1 + h > df.h) {
        return;
    }
    bool overlap = (sf.ptr == df.ptr);
    if (overlap && x == x1 && y == y1) {
        return;
    }

    if (skip < 0 && !hdmi_rgb121) {
        // Opaque, byte-addressable (8/16bpp): one memmove per row (memmove
        // covers horizontal overlap); iterate bottom-up when the destination
        // is below the source so vertical overlap is safe too.
        int bpp = hdmi_native ? 1 : 2;
        size_t sstride = (size_t)sf.w * bpp;
        size_t dstride = (size_t)df.w * bpp;
        size_t nbytes = (size_t)w * bpp;
        if (overlap && y1 > y) {
            for (int j = h - 1; j >= 0; j--) {
                memmove(df.ptr + (size_t)(y1 + j) * dstride + (size_t)x1 * bpp,
                    sf.ptr + (size_t)(y + j) * sstride + (size_t)x * bpp, nbytes);
            }
        } else {
            for (int j = 0; j < h; j++) {
                memmove(df.ptr + (size_t)(y1 + j) * dstride + (size_t)x1 * bpp,
                    sf.ptr + (size_t)(y + j) * sstride + (size_t)x * bpp, nbytes);
            }
        }
        return;
    }

    // Per-pixel path: 4bpp packed buffers and/or a skip colour. Row and
    // column directions are chosen so overlapping same-buffer copies never
    // read a pixel this blit already wrote.
    int j0 = 0, jend = h, jstep = 1;
    if (overlap && y1 > y) {
        j0 = h - 1;
        jend = -1;
        jstep = -1;
    }
    int i0 = 0, iend = w, istep = 1;
    if (overlap && y1 == y && x1 > x) {
        i0 = w - 1;
        iend = -1;
        istep = -1;
    }
    for (int j = j0; j != jend; j += jstep) {
        for (int i = i0; i != iend; i += istep) {
            mp_int_t v = hdmi_px_get(sf.ptr, sf.w, x + i, y + j);
            if (v != skip) {
                hdmi_px_set(df.ptr, df.w, x1 + i, y1 + j, v);
            }
        }
    }
}

// hdmi.blit(x, y, w, h, x1, y1 [, src [, dst [, skip]]]) -- copy the w x h
// rectangle at (x,y) of `src` to (x1,y1) of `dst`. src/dst are target letters
// ("N"/"L"/"F"), (buffer, w, h) surfaces, or None/omitted for the current
// write target — so it blits within one buffer, between buffers, or to/from
// user buffers such as sprite images. `skip` is a native-format colour that
// is NOT copied (source pixels of that colour leave the destination alone,
// -1/default = copy everything). Clipping follows MMBasic's BLIT: a rectangle
// partly off either surface is trimmed on both sides in step. Overlapping
// same-buffer copies are safe in any direction (MMBasic BLIT semantics).
static mp_obj_t hdmi_blit(size_t n_args, const mp_obj_t *args) {
    if (!hdmi_running) {
        mp_raise_ValueError(MP_ERROR_TEXT("display not initialised"));
    }
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int w = mp_obj_get_int(args[2]);
    int h = mp_obj_get_int(args[3]);
    int x1 = mp_obj_get_int(args[4]);
    int y1 = mp_obj_get_int(args[5]);
    hdmi_surf_t sf = hdmi_parse_surface((n_args > 6) ? args[6] : mp_const_none);
    hdmi_surf_t df = hdmi_parse_surface((n_args > 7) ? args[7] : mp_const_none);
    mp_int_t skip = (n_args > 8) ? mp_obj_get_int(args[8]) : -1;
    hdmi_do_blit(sf, df, x, y, w, h, x1, y1, skip);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_blit_obj, 6, 9, hdmi_blit);

// hdmi.tilemap(map, cols, rows, tileset, tiles_per_row, tw, th, vx, vy,
//              sx, sy, vw, vh [, skip [, dst]]) -- render a tile map (MMBasic
// TILEMAP DRAW). `map` is a buffer of cols*rows uint16 tile indices (1-based,
// 0 = empty). `tileset` is the tile-sheet surface (a (buffer,w,h) tuple, e.g.
// load_image().surface, or a target letter). The viewport at world pixel
// (vx,vy), size vw x vh, is drawn to `dst` at (sx,sy) with a sub-tile offset
// for smooth scrolling; partial edge tiles are clipped. `skip` is a transparent
// colour (-1 = opaque). Just a fast per-tile hdmi_do_blit loop, in C.
static mp_obj_t hdmi_tilemap(size_t n_args, const mp_obj_t *args) {
    if (!hdmi_running) {
        mp_raise_ValueError(MP_ERROR_TEXT("display not initialised"));
    }
    mp_buffer_info_t mi;
    mp_get_buffer_raise(args[0], &mi, MP_BUFFER_READ);
    const uint16_t *map = (const uint16_t *)mi.buf;
    int cols = mp_obj_get_int(args[1]);
    int rows = mp_obj_get_int(args[2]);
    hdmi_surf_t ts = hdmi_parse_surface(args[3]);
    int tpr = mp_obj_get_int(args[4]);
    int tw = mp_obj_get_int(args[5]);
    int th = mp_obj_get_int(args[6]);
    int vx = mp_obj_get_int(args[7]);
    int vy = mp_obj_get_int(args[8]);
    int sx = mp_obj_get_int(args[9]);
    int sy = mp_obj_get_int(args[10]);
    int vw = mp_obj_get_int(args[11]);
    int vh = mp_obj_get_int(args[12]);
    mp_int_t skip = (n_args > 13) ? mp_obj_get_int(args[13]) : -1;
    hdmi_surf_t df = hdmi_parse_surface((n_args > 14) ? args[14] : mp_const_none);
    if (cols < 1 || rows < 1 || tw < 1 || th < 1 || tpr < 1) {
        return mp_const_none;
    }
    if ((size_t)cols * rows * 2 > mi.len) {
        mp_raise_ValueError(MP_ERROR_TEXT("map buffer too small"));
    }
    // Floor-divide the viewport origin to the first visible cell + sub-tile
    // pixel offset (correct for negative viewports too).
    int col_start = vx / tw, off_x = vx % tw;
    if (off_x < 0) {
        off_x += tw;
        col_start -= 1;
    }
    int row_start = vy / th, off_y = vy % th;
    if (off_y < 0) {
        off_y += th;
        row_start -= 1;
    }
    int col_end = (vx + vw - 1) / tw;
    int row_end = (vy + vh - 1) / th;
    for (int r = row_start; r <= row_end; r++) {
        if (r < 0 || r >= rows) {
            continue;
        }
        for (int c = col_start; c <= col_end; c++) {
            if (c < 0 || c >= cols) {
                continue;
            }
            int tile = map[c + r * cols];
            if (tile == 0) {
                continue;   // empty
            }
            int src_x = ((tile - 1) % tpr) * tw;
            int src_y = ((tile - 1) / tpr) * th;
            int dst_x = sx + (c - col_start) * tw - off_x;
            int dst_y = sy + (r - row_start) * th - off_y;
            hdmi_do_blit(ts, df, src_x, src_y, tw, th, dst_x, dst_y, skip);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_tilemap_obj, 13, 15, hdmi_tilemap);

// Push a seed point onto the flood stack, growing it if full. Returns the
// (possibly moved) stack base.
static int32_t *hdmi_flood_push(int32_t *stack, int *sp, int *cap, int px, int py) {
    if (*sp >= *cap) {
        int nc = *cap * 2;
        stack = m_renew(int32_t, stack, (size_t)*cap * 2, (size_t)nc * 2);
        *cap = nc;
    }
    stack[*sp * 2] = px;
    stack[*sp * 2 + 1] = py;
    (*sp)++;
    return stack;
}

// hdmi.flood(x, y, colour [, border]) -- scanline flood fill of the current
// write target (MMBasic floodfill). Without `border` (or border < 0): FLOOD
// mode — replace the contiguous region of the seed pixel's colour with
// `colour` (a paint-bucket). With `border` >= 0: BOUNDARY mode — fill outward
// from the seed over any colour, stopping at pixels of the `border` colour.
// Colours are native-format (as fill/blit); works in every video mode.
static mp_obj_t hdmi_flood(size_t n_args, const mp_obj_t *args) {
    if (!hdmi_running) {
        mp_raise_ValueError(MP_ERROR_TEXT("display not initialised"));
    }
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    mp_int_t fill = mp_obj_get_int(args[2]);
    mp_int_t border = (n_args > 3) ? mp_obj_get_int(args[3]) : -1;
    int W = hdmi_w, H = hdmi_h;
    if (x < 0 || x >= W || y < 0 || y >= H) {
        return mp_const_none;
    }
    uint8_t *buf = hdmi_wbuf();
    mp_int_t seed = hdmi_px_get(buf, W, x, y);
    bool boundary = (border >= 0);
    // Nothing to do if the seed is already the boundary, or (flood mode) is
    // already the fill colour — both would loop forever otherwise.
    if (boundary ? (seed == border) : (seed == fill)) {
        return mp_const_none;
    }
    #define HDMI_FLOOD_MATCH(px) \
        (boundary ? ((px) != border && (px) != fill) : ((px) == seed))

    int cap = 256, sp = 0;
    int32_t *stack = m_new(int32_t, (size_t)cap * 2);
    stack = hdmi_flood_push(stack, &sp, &cap, x, y);
    while (sp > 0) {
        sp--;
        int sx = stack[sp * 2], sy = stack[sp * 2 + 1];
        if (!HDMI_FLOOD_MATCH(hdmi_px_get(buf, W, sx, sy))) {
            continue;
        }
        int x1 = sx, x2 = sx;
        while (x1 > 0 && HDMI_FLOOD_MATCH(hdmi_px_get(buf, W, x1 - 1, sy))) {
            x1--;
        }
        while (x2 < W - 1 && HDMI_FLOOD_MATCH(hdmi_px_get(buf, W, x2 + 1, sy))) {
            x2++;
        }
        bool above = false, below = false;
        for (int i = x1; i <= x2; i++) {
            hdmi_px_set(buf, W, i, sy, fill);
            if (sy > 0) {
                bool m = HDMI_FLOOD_MATCH(hdmi_px_get(buf, W, i, sy - 1));
                if (m && !above) {
                    stack = hdmi_flood_push(stack, &sp, &cap, i, sy - 1);
                    above = true;
                } else if (!m) {
                    above = false;
                }
            }
            if (sy < H - 1) {
                bool m = HDMI_FLOOD_MATCH(hdmi_px_get(buf, W, i, sy + 1));
                if (m && !below) {
                    stack = hdmi_flood_push(stack, &sp, &cap, i, sy + 1);
                    below = true;
                } else if (!m) {
                    below = false;
                }
            }
        }
    }
    #undef HDMI_FLOOD_MATCH
    m_free(stack);  // m_new blocks are GC-managed; free promptly anyway
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_flood_obj, 3, 4, hdmi_flood);

// MMBasic turtle fill patterns (PicoMite Turtle.c fill_patterns, verbatim):
// 8x8 bitmaps, row = pattern[y & 7], bit = 1 << (x & 7), anchored to screen
// coordinates so adjacent patterned shapes tile seamlessly. 0 is solid.
static const uint8_t hdmi_fill_patterns[32][8] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, // 0: Solid
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}, // 1: Checkerboard
    {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88}, // 2: Vertical lines
    {0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}, // 3: Horizontal lines
    {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}, // 4: Diagonal cross
    {0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88}, // 5: Diagonal stripes
    {0xC3, 0xC3, 0x3C, 0x3C, 0xC3, 0xC3, 0x3C, 0x3C}, // 6: Crosshatch
    {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01}, // 7: Diagonal fine
    {0x99, 0x66, 0x99, 0x66, 0x99, 0x66, 0x99, 0x66}, // 8: Dense checkerboard
    {0x92, 0x49, 0x24, 0x92, 0x49, 0x24, 0x92, 0x49}, // 9: Diagonal right medium
    {0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0x49, 0x92}, // 10: Diagonal left medium
    {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, // 11: Vertical lines medium
    {0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00}, // 12: Horizontal lines medium
    {0xF0, 0xF0, 0xF0, 0xF0, 0x0F, 0x0F, 0x0F, 0x0F}, // 13: Large checkerboard
    {0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00}, // 14: Dotted vertical
    {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00}, // 15: Horizontal stripes tight
    {0x81, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x81, 0xFF}, // 16: Grid
    {0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55}, // 17: Weave pattern
    {0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18}, // 18: Diamond
    {0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF}, // 19: Gradient diagonal
    {0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF}, // 20: Gradient diagonal reverse
    {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF}, // 21: Border/frame
    {0xC0, 0xC0, 0xC0, 0xC0, 0x03, 0x03, 0x03, 0x03}, // 22: Vertical split
    {0x66, 0x99, 0x99, 0x66, 0x66, 0x99, 0x99, 0x66}, // 23: Woven
    {0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00}, // 24: Sparse dots
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}, // 25: Diagonal very fine
    {0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10, 0x00}, // 26: Arrow up
    {0xDB, 0xDB, 0xDB, 0x00, 0xDB, 0xDB, 0xDB, 0x00}, // 27: Dense dots
    {0xE7, 0xC3, 0x81, 0x00, 0x81, 0xC3, 0xE7, 0xFF}, // 28: Chevron
    {0x18, 0x24, 0x42, 0x81, 0x81, 0x42, 0x24, 0x18}, // 29: Diamond hollow
    {0x3C, 0x42, 0x81, 0x81, 0x81, 0x81, 0x42, 0x3C}, // 30: Circle
    {0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E}, // 31: Circle filled
};

// Mode-aware RGB888 -> native colour (RGB332 / RGB565 / RGB121 palette
// index), for C renderers that carry MMBasic's 24-bit colours (draw3d).
int32_t hdmi_colour_native(uint32_t rgb888) {
    int r = (rgb888 >> 16) & 0xFF, g = (rgb888 >> 8) & 0xFF, b = rgb888 & 0xFF;
    if (hdmi_rgb121) {
        return hdmi_nearest_index(r, g, b);
    }
    if (hdmi_native) {
        return (r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6);
    }
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Clipped 1-px Bresenham line on the current write target (MMBasic DrawLine
// width 1, as the 3D engine uses it).
void hdmi_draw_line_raw(int x1, int y1, int x2, int y2, int32_t colour) {
    uint8_t *buf = hdmi_wbuf();
    int dx = x2 - x1, sx = dx < 0 ? -1 : 1;
    int dy = y2 - y1, sy = dy < 0 ? -1 : 1;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    int err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        if (x1 >= 0 && x1 < hdmi_w && y1 >= 0 && y1 < hdmi_h) {
            hdmi_px_set(buf, hdmi_w, x1, y1, colour);
        }
        if (x1 == x2 && y1 == y2) {
            break;
        }
        int e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y1 += sy;
        }
    }
}

// Clipped single pixel on the current write target (MMBasic DrawPixel, as
// the 3D hidden-line edge drawer uses it).
void hdmi_pixel_raw(int x, int y, int32_t colour) {
    if (x >= 0 && x < hdmi_w && y >= 0 && y < hdmi_h) {
        hdmi_px_set(hdmi_wbuf(), hdmi_w, x, y, colour);
    }
}

// Fast horizontal span x1..x2 (inclusive, caller-clipped) on a target
// buffer -- runs written directly per mode, as MMBasic's DrawFill does,
// instead of per-pixel calls.
static void hdmi_hspan(uint8_t *buf, int x1, int x2, int y, mp_int_t colour) {
    if (x2 < x1) {
        return;
    }
    if (hdmi_rgb121) {
        uint8_t *row = &buf[(size_t)y * (hdmi_w / 2)];
        if (x1 & 1) { // odd x = low nibble (GS4_HMSB)
            row[x1 >> 1] = (row[x1 >> 1] & 0xf0) | (uint8_t)(colour & 0x0f);
            x1++;
        }
        if (!(x2 & 1)) { // even x = high nibble
            row[x2 >> 1] = (row[x2 >> 1] & 0x0f) | (uint8_t)((colour & 0x0f) << 4);
            x2--;
        }
        if (x1 <= x2) {
            memset(&row[x1 >> 1], (uint8_t)((colour & 0x0f) * 0x11), (size_t)((x2 - x1 + 1) >> 1));
        }
    } else if (hdmi_native) {
        memset(&buf[(size_t)y * hdmi_w + x1], (uint8_t)colour, (size_t)(x2 - x1 + 1));
    } else {
        uint16_t *row = &((uint16_t *)buf)[(size_t)y * hdmi_w + x1];
        uint16_t c = (uint16_t)colour;
        for (int n = x2 - x1 + 1; n > 0; n--) {
            *row++ = c;
        }
    }
}

// Clipped filled rectangle on the current write target (MMBasic
// DrawRectangle, as the 3D engine's hide/clear uses it).
void hdmi_fill_rect_raw(int x1, int y1, int x2, int y2, int32_t colour) {
    uint8_t *buf = hdmi_wbuf();
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 > hdmi_w - 1) {
        x2 = hdmi_w - 1;
    }
    if (y2 > hdmi_h - 1) {
        y2 = hdmi_h - 1;
    }
    for (int y = y1; y <= y2; y++) {
        hdmi_hspan(buf, x1, x2, y, colour);
    }
}

// The MMBasic turtle polygon fill (see hdmi.polyfill below), callable from C.
void hdmi_polyfill_raw(const int16_t *pts, int count, int32_t colour, int pattern) {
    if (pattern < 0 || pattern > 31) {
        pattern = 0;
    }
    if (count < 3) {
        return;
    }
    uint8_t *buf = hdmi_wbuf();
    const int W = hdmi_w, H = hdmi_h;
    int min_y = pts[1], max_y = pts[1];
    for (int i = 1; i < count; i++) {
        int yy = pts[i * 2 + 1];
        if (yy < min_y) {
            min_y = yy;
        }
        if (yy > max_y) {
            max_y = yy;
        }
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_y > H - 1) {
        max_y = H - 1;
    }
    const uint8_t *prow = hdmi_fill_patterns[pattern];
    for (int scan_y = min_y; scan_y <= max_y; scan_y++) {
        int inter[256];
        int n = 0;
        for (int i = 0; i < count; i++) {
            int next = (i + 1) % count;
            int y1 = pts[i * 2 + 1], y2 = pts[next * 2 + 1];
            if ((y1 <= scan_y && y2 > scan_y) || (y2 <= scan_y && y1 > scan_y)) {
                int x1 = pts[i * 2], x2 = pts[next * 2];
                int ix = x1 + (scan_y - y1) * (x2 - x1) / (y2 - y1);
                if (n < 256) {
                    inter[n++] = ix;
                }
            }
        }
        for (int i = 0; i < n - 1; i++) { // sort crossings (MMBasic verbatim)
            for (int j = i + 1; j < n; j++) {
                if (inter[i] > inter[j]) {
                    int t = inter[i];
                    inter[i] = inter[j];
                    inter[j] = t;
                }
            }
        }
        uint8_t bits = prow[scan_y & 7];
        for (int i = 0; i + 1 < n; i += 2) {
            int xa = inter[i], xb = inter[i + 1];
            if (xa < 0) {
                xa = 0;
            }
            if (xb > W - 1) {
                xb = W - 1;
            }
            if (bits == 0xFF) {
                // Solid row: write the span as a run (MMBasic's DrawFill).
                hdmi_hspan(buf, xa, xb, scan_y, colour);
            } else {
                for (int px = xa; px <= xb; px++) {
                    if (bits & (1u << (px & 7))) {
                        hdmi_px_set(buf, W, px, scan_y, colour);
                    }
                }
            }
        }
    }
}

// hdmi.polyfill(points, colour, pattern=0) -- fill a polygon on the current
// write target: the MMBasic turtle's scanline fill (fill_polygon_scanline /
// fill_polygon_pattern from PicoMite Turtle.c, ported verbatim: even-odd
// spans, up to 256 edge crossings per scanline). `points` is a flat buffer
// of int16 x,y pairs (as framebuf.poly takes). pattern 0 = solid, 1..31 =
// the MMBasic pattern set; a pattern's unset bits leave the background.
static mp_obj_t hdmi_polyfill_fn(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    int pattern = (n_args > 2) ? mp_obj_get_int(args[2]) : 0;
    hdmi_polyfill_raw((const int16_t *)bufinfo.buf, (int)(bufinfo.len / 4),
        (int32_t)mp_obj_get_int(args[1]), pattern);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_polyfill_obj, 2, 3, hdmi_polyfill_fn);

// hdmi.close("L"/"F") or hdmi.close() for both -- drop the layer (the overlay
// disappears; the display underneath is untouched) and/or release the F buffer.
// If the write target was closed, drawing returns to the display (MMBasic).
static mp_obj_t hdmi_close(size_t n_args, const mp_obj_t *args) {
    bool close_l = true, close_f = true;
    if (n_args > 0) {
        int target = hdmi_parse_target(args[0]);
        if (target == HDMI_TARGET_N) {
            mp_raise_ValueError(MP_ERROR_TEXT("cannot close the display"));
        }
        close_l = (target == HDMI_TARGET_L);
        close_f = (target == HDMI_TARGET_F);
    }
    if (close_l) {
        hdmi_layer_on = 0;
        if (hdmi_target == HDMI_TARGET_L) {
            hdmi_target = HDMI_TARGET_N;
        }
    }
    if (close_f) {
        if (hdmi_target == HDMI_TARGET_F) {
            hdmi_target = HDMI_TARGET_N;
        }
        MP_STATE_PORT(hdmi_framebuf_f) = NULL; // the GC reclaims it
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_close_obj, 0, 1, hdmi_close);

static const mp_rom_map_elem_t hdmi_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_hdmi) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&hdmi_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&hdmi_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_framebuffer), MP_ROM_PTR(&hdmi_framebuffer_obj) },
    { MP_ROM_QSTR(MP_QSTR_fb), MP_ROM_PTR(&hdmi_fb_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&hdmi_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_scroll), MP_ROM_PTR(&hdmi_scroll_obj) },
    { MP_ROM_QSTR(MP_QSTR_putc), MP_ROM_PTR(&hdmi_putc_obj) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&hdmi_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_fonts), MP_ROM_PTR(&hdmi_fonts_obj) },
    { MP_ROM_QSTR(MP_QSTR_stack_ok), MP_ROM_PTR(&hdmi_stack_ok_obj) },
    { MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&hdmi_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&hdmi_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_gen), MP_ROM_PTR(&hdmi_gen_obj) },
    { MP_ROM_QSTR(MP_QSTR_test), MP_ROM_PTR(&hdmi_test_obj) },
    { MP_ROM_QSTR(MP_QSTR_rgb565), MP_ROM_PTR(&hdmi_rgb565_obj) },
    { MP_ROM_QSTR(MP_QSTR_bpp), MP_ROM_PTR(&hdmi_bpp_obj) },
    { MP_ROM_QSTR(MP_QSTR_palette), MP_ROM_PTR(&hdmi_palette_obj) },
    // MMBasic FRAMEBUFFER: overlay layer, off-screen buffer, target select, copy.
    { MP_ROM_QSTR(MP_QSTR_layer), MP_ROM_PTR(&hdmi_layer_obj) },
    { MP_ROM_QSTR(MP_QSTR_transparent), MP_ROM_PTR(&hdmi_transparent_obj) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&hdmi_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&hdmi_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy), MP_ROM_PTR(&hdmi_copy_obj) },
    { MP_ROM_QSTR(MP_QSTR_blit), MP_ROM_PTR(&hdmi_blit_obj) },
    { MP_ROM_QSTR(MP_QSTR_tilemap), MP_ROM_PTR(&hdmi_tilemap_obj) },
    { MP_ROM_QSTR(MP_QSTR_flood), MP_ROM_PTR(&hdmi_flood_obj) },
    { MP_ROM_QSTR(MP_QSTR_polyfill), MP_ROM_PTR(&hdmi_polyfill_obj) },
    { MP_ROM_QSTR(MP_QSTR_vsync), MP_ROM_PTR(&hdmi_vsync_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&hdmi_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_RGB640), MP_ROM_INT(HDMI_MODE_RGB640) },
    { MP_ROM_QSTR(MP_QSTR_RGB320), MP_ROM_INT(HDMI_MODE_RGB320) },
    { MP_ROM_QSTR(MP_QSTR_RGB512), MP_ROM_INT(HDMI_MODE_RGB512) },
    { MP_ROM_QSTR(MP_QSTR_RGB1024), MP_ROM_INT(HDMI_MODE_RGB1024) },
    { MP_ROM_QSTR(MP_QSTR_RGB640_4), MP_ROM_INT(HDMI_MODE_RGB640_4) },
};
static MP_DEFINE_CONST_DICT(hdmi_module_globals, hdmi_module_globals_table);

const mp_obj_module_t hdmi_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&hdmi_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_hdmi, hdmi_module);

// The off-screen F buffer (hdmi.create()) lives on the GC heap (PSRAM) with no
// necessary Python reference, so it must be a GC root to stay alive.
MP_REGISTER_ROOT_POINTER(uint8_t *hdmi_framebuf_f);

#endif // MICROPY_HW_ENABLE_HDMI
