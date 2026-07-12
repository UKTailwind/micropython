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

#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/dma.h"
#include "pico/stdlib.h"   // set_sys_clock_khz, setup_default_uart
#include "uart.h"          // mp_uart_init
#include "rp2_flash.h"     // rp2_flash_set_timing_for_freq
#include "rp2_psram.h"     // psram_init

#if MICROPY_PY_NETWORK_CYW43 || MICROPY_PY_BLUETOOTH_CYW43
#include "hardware/pio.h"
#include "lib/cyw43-driver/src/cyw43.h"           // cyw43_state
#include "lib/cyw43-driver/src/cyw43_internal.h"  // cyw43_int_t (for .bus_data)
// Mirror of the SDK's file-static bus_data_t (cyw43_bus_pio_spi.c). We only read
// the live PIO/SM to retune the gSPI clock divider after a clk_sys change.
typedef struct {
    PIO pio;
    uint pio_offset;
    uint pio_sm;
    int8_t dma_out, dma_in;
} hdmi_cyw43_bus_t;
#endif

// MMBasic 8x12 console font (font1): header {w=8,h=12,first=0x20,count=224},
// then 12 bytes/glyph, one row each, MSB = leftmost pixel.
#include "console_font.h"
#define FONT_W     (8)
#define FONT_H     (12)
#define FONT_FIRST (0x20)

// --- Video timing, hardcoded per resolution. The per-scanline DMA IRQ is in
// the scanout hot path, so (like MMBasic) each resolution gets its OWN IRQ with
// these as compile-time constants — a single parameterised IRQ is too slow.
// Both reuse fixed negative-sync command lists (MMBasic never applies the
// MODE_*_SYNC_POLARITY metadata to the HSTX output, so 1024x600's positive
// H-sync isn't special-cased either).
//
// 640x480@60 (RGB640 / RGB320):
#define MODE_H_FRONT_PORCH   (16)
#define MODE_H_SYNC_WIDTH    (96)
#define MODE_H_BACK_PORCH    (48)
#define MODE_H_ACTIVE_PIXELS (640)
#define MODE_V_FRONT_PORCH   (10)
#define MODE_V_SYNC_WIDTH    (2)
#define MODE_V_BACK_PORCH    (33)
#define MODE_V_ACTIVE_LINES  (480)
#define MODE_V_TOTAL_LINES   (525)
#define BLANKING_COUNT       (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH) // 45

// 1024x600@60 (RGB512), from MMBasic Screens.h "X"; pixel clock = clk_sys/5:
#define X_H_FRONT_PORCH      (24)
#define X_H_SYNC_WIDTH       (136)
#define X_H_BACK_PORCH       (160)
#define X_H_ACTIVE_PIXELS    (1024)
#define X_V_FRONT_PORCH      (1)
#define X_V_SYNC_WIDTH       (4)
#define X_V_BACK_PORCH       (23)
#define X_V_ACTIVE_LINES     (600)
#define X_V_TOTAL_LINES      (628)
#define X_BLANKING_COUNT     (X_V_FRONT_PORCH + X_V_SYNC_WIDTH + X_V_BACK_PORCH) // 28

#define HDMI_MAX_H (1024) // widest active line (RGB512); sizes HDMIlines

// Modes (Python: hdmi.RGB640 / hdmi.RGB320 / hdmi.RGB512 / hdmi.RGB1024). Each is
// named for its framebuffer width; the pixel format is separate (RGB332 / RGB565 /
// RGB121).
#define HDMI_MODE_RGB640 (0) // 640x480 RGB332 (8bpp), native scan
#define HDMI_MODE_RGB320 (1) // 320x240 RGB565, pixel+line doubled -> 640x480
#define HDMI_MODE_RGB512 (2) // 512x300 RGB565, pixel+line doubled -> 1024x600
#define HDMI_MODE_RGB1024 (3) // 1024x600, RGB121 4bpp packed format, native res;
                              // core1 expands each line through a 16-colour palette
                              // into an RGB332 line buffer that HSTX scans natively.

// Largest framebuffer: 640x480x8 = 512x300x2 = 1024*600/2 = 307200 bytes. The
// RGB121 1024x600x4 framebuffer is an exact fit in this same array.
#define HDMI_FB_BYTES (640 * 480)

// --- HDMI lane -> HSTX bit mapping (bit N appears on GP12+N) ---------------
#ifndef MICROPY_HW_HDMI_CLK
#define MICROPY_HW_HDMI_CLK (2)
#endif
#ifndef MICROPY_HW_HDMI_D0
#define MICROPY_HW_HDMI_D0  (0)
#endif
#ifndef MICROPY_HW_HDMI_D1
#define MICROPY_HW_HDMI_D1  (6)
#endif
#ifndef MICROPY_HW_HDMI_D2
#define MICROPY_HW_HDMI_D2  (4)
#endif

// --- HSTX command words and TMDS control symbols --------------------------
#define HSTX_CMD_RAW        (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_TMDS       (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP        (0xfu << 12)

#define TMDS_CTRL_00 (0x354u)
#define TMDS_CTRL_01 (0x0abu)
#define TMDS_CTRL_10 (0x154u)
#define TMDS_CTRL_11 (0x2abu)
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

// --- State (all SRAM) ------------------------------------------------------
static uint8_t hdmi_fb[HDMI_FB_BYTES] __attribute__((aligned(4)));
static uint16_t HDMIlines[2][HDMI_MAX_H]; // RGB565 doubled scanlines (up to 1024)
static uint32_t vblank_line_vsync_off[7];
static uint32_t vblank_line_vsync_on[7];
static uint32_t vactive_line[9];

static volatile int32_t v_scanline = 2;
static volatile bool dma_pong = false;
static volatile bool vactive_cmdlist_posted = false;
static int dmach_ping = -1, dmach_pong = -1;
static volatile bool hdmi_running = false;
static irq_handler_t hdmi_installed_irq = NULL; // current DMA_IRQ_1 handler (for removal on deinit)

static int hdmi_mode = HDMI_MODE_RGB640;
static int hdmi_w = 640, hdmi_h = 480;    // logical framebuffer dimensions
static int hdmi_transfer_count = 160;     // active pixel words per line
static int hdmi_native = 1;               // 1 = native 8bpp scan (RGB640); 0 = doubled RGB565
static int hdmi_rgb121 = 0;               // 1 = 4bpp packed framebuffer, core1 expands to an RGB332 line buffer

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
static uint16_t hdmi_map256[256];

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
        hdmi_map256[b] = (uint16_t)(p332[b & 0x0f] | (p332[b >> 4] << 8));
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
static uint32_t hdmi_clock_khz = 252000;  // clk_sys for this init (252/315/378)
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
static volatile int hdmi_layer_on = 0;          // core1 merges the layer when set
static volatile uint16_t hdmi_layer_transp = 0; // layer transparent colour (RGB565)

// Bytes in one mode-sized framebuffer (the drawable size, not the static array).
static size_t hdmi_fb_bytes(void) {
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

#define HDMI_CORE1_STACK_WORDS (1024) // 4 KB
#define HDMI_STACK_SENTINEL    (0xf00dbeefu)
static uint32_t hdmi_core1_stack[HDMI_CORE1_STACK_WORDS] __attribute__((aligned(8)));

// --- DMA IRQ: post the next scanline's command list / pixels (RAM-resident)
static void __not_in_flash_func(hdmi_dma_irq)(void) {
    uint ch_num = dma_pong ? dmach_pong : dmach_ping;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->ints1 = 1u << ch_num; // ack on DMA IRQ line 1
    dma_pong = !dma_pong;

    if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < BLANKING_COUNT) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        int active = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
        if (hdmi_native) {
            // Native 640x480x8: scan the framebuffer line directly.
            ch->read_addr = (uintptr_t)&hdmi_fb[active * MODE_H_ACTIVE_PIXELS];
        } else {
            // 320x240 RGB565: the core1 fill-loop has doubled this line.
            ch->read_addr = (uintptr_t)HDMIlines[v_scanline & 1];
        }
        ch->transfer_count = hdmi_transfer_count;
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
    }
}

// --- Separate DMA IRQ for 1024x600 (RGB512), all timing hardcoded. RGB512 is
// always doubled RGB565, so the active line is always a core1-filled HDMIlines.
static void __not_in_flash_func(hdmi_dma_irq_x)(void) {
    uint ch_num = dma_pong ? dmach_pong : dmach_ping;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->ints1 = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= X_V_FRONT_PORCH && v_scanline < (X_V_FRONT_PORCH + X_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < X_BLANKING_COUNT) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)HDMIlines[v_scanline & 1];
        ch->transfer_count = X_H_ACTIVE_PIXELS / 2; // 512 words (2 RGB565 px/word)
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % X_V_TOTAL_LINES;
    }
}

// --- Separate DMA IRQ for RGB1024 (1024x600 native). Same 1024x600 timing as
// hdmi_dma_irq_x, but the active line is a core1-filled *RGB332* buffer scanned
// as 4 pixels/word (256 words), not RGB565 (512 words).
static void __not_in_flash_func(hdmi_dma_irq_1024)(void) {
    uint ch_num = dma_pong ? dmach_pong : dmach_ping;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->ints1 = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= X_V_FRONT_PORCH && v_scanline < (X_V_FRONT_PORCH + X_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < X_BLANKING_COUNT) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)HDMIlines[v_scanline & 1];
        ch->transfer_count = X_H_ACTIVE_PIXELS / 4; // 256 words (4 RGB332 px/word)
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % X_V_TOTAL_LINES;
    }
}

// --- core1 fill loop: RGB565 horizontal doubling into the next line buffer -
// (RGB332 is native, so this returns immediately and core1 idles.)
static void __not_in_flash_func(hdmi_fill_loop)(void) {
    if (hdmi_native) {
        while (hdmi_running) { // RGB640 scans the framebuffer directly; core1 idles
            __wfe();
        }
        return;
    }
    if (hdmi_rgb121) {
        // 1024x600 native: expand 512 packed bytes per source row into 1024 RGB332
        // pixels. One SRAM lookup + one 16-bit store per source byte (two pixels),
        // no doubling. hdmi_map256/hdmi_fb/HDMIlines are all SRAM (no flash reads).
        const uint16_t *m = hdmi_map256;
        int last_line = 2;
        while (hdmi_running) {
            if (v_scanline != last_line) {
                last_line = v_scanline;
                int active = v_scanline - (X_V_TOTAL_LINES - X_V_ACTIVE_LINES);
                uint16_t *p = (uint16_t *)HDMIlines[last_line & 1];
                if (active >= 0 && active < X_V_ACTIVE_LINES) {
                    const uint8_t *s = &hdmi_fb[active * (X_H_ACTIVE_PIXELS / 2)];
                    for (int i = 0; i < X_H_ACTIVE_PIXELS / 2; i++) {
                        p[i] = m[s[i]];
                    }
                }
            }
        }
        return;
    }
    const uint16_t *fb16 = (const uint16_t *)hdmi_fb;
    int last_line = 2;
    if (hdmi_mode == HDMI_MODE_RGB512) {
        // 512x300 -> 1024x600: double 512 source px to 1024, vertical double.
        while (hdmi_running) {
            if (v_scanline != last_line) {
                last_line = v_scanline;
                int active = v_scanline - (X_V_TOTAL_LINES - X_V_ACTIVE_LINES);
                uint16_t *p = HDMIlines[last_line & 1];
                if (active >= 0 && active < X_V_ACTIVE_LINES) {
                    const uint16_t *s = &fb16[(active >> 1) * 512];
                    for (int i = 0; i < 512; i++) {
                        uint16_t v = s[i];
                        *p++ = v;
                        *p++ = v;
                    }
                }
            }
        }
    } else {
        // 320x240 -> 640x480: double 320 source px to 640, vertical double.
        // With the layer enabled (hdmi.layer()), merge per pixel first: the
        // layer pixel wins unless it equals the transparent colour (verbatim
        // MMBasic's HDMI layer merge). The layer is the second half of the
        // static video memory, so every read here stays SRAM.
        const uint16_t *layer16 = (const uint16_t *)(hdmi_fb + 320 * 240 * 2);
        while (hdmi_running) {
            if (v_scanline != last_line) {
                last_line = v_scanline;
                int active = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
                uint16_t *p = HDMIlines[last_line & 1];
                if (active >= 0 && active < MODE_V_ACTIVE_LINES) {
                    const uint16_t *s = &fb16[(active >> 1) * 320];
                    if (hdmi_layer_on) {
                        const uint16_t *l = &layer16[(active >> 1) * 320];
                        uint16_t t = hdmi_layer_transp;
                        for (int i = 0; i < 320; i++) {
                            uint16_t v = l[i];
                            if (v == t) {
                                v = s[i];
                            }
                            *p++ = v;
                            *p++ = v;
                        }
                    } else {
                        for (int i = 0; i < 320; i++) {
                            uint16_t v = s[i];
                            *p++ = v;
                            *p++ = v;
                        }
                    }
                }
            }
        }
    }
}

// --- core1 entry: bring up clk_hstx, HSTX, DMA, IRQ, then run the fill loop
static void __not_in_flash_func(hdmi_core1_entry)(void) {
    // Pixel clock = clk_hstx / 5 (CSR CLKDIV=5). Keep it ~25.2 MHz for 640x480:
    //   252 MHz -> clk_hstx = clk_sys/2 = 126 MHz -> 25.2 MHz pixel (60 Hz)
    //   315 MHz -> clk_hstx = clk_sys/2 = 157.5 MHz -> 31.5 MHz pixel (75 Hz)
    //   378 MHz -> clk_hstx = clk_sys*332/1000 = 125.5 MHz -> 25.1 MHz pixel (60 Hz)
    // (ratios match MMBasic's HDMI.c; the 378 case needs the fractional divider
    // because clk_sys/2 = 189 MHz would be far too fast.)
    // RGB512 (doubled) and RGB1024 (native) both drive the 1024x600 timing.
    const bool use_x_timing = (hdmi_mode == HDMI_MODE_RGB512 || hdmi_mode == HDMI_MODE_RGB1024);
    uint32_t hstx_in = clock_get_hz(clk_sys);
    uint32_t hstx_target;
    if (use_x_timing) {
        hstx_target = hstx_in; // clk_hstx = clk_sys -> pixel = clk_sys/5 = 50.4 MHz (1024x600)
    } else if (hdmi_clock_khz == 378000) {
        hstx_target = (uint32_t)(((uint64_t)hstx_in * 332) / 1000);
    } else {
        hstx_target = hstx_in / 2;
    }
    clock_configure(clk_hstx, 0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        hstx_in, hstx_target);

    // Per-scanline H timing for the command lists (one-time; the hot-path DMA
    // IRQ uses hardcoded constants via its own handler).
    const int h_front = use_x_timing ? X_H_FRONT_PORCH : MODE_H_FRONT_PORCH;
    const int h_sync = use_x_timing ? X_H_SYNC_WIDTH : MODE_H_SYNC_WIDTH;
    const int h_back = use_x_timing ? X_H_BACK_PORCH : MODE_H_BACK_PORCH;
    const int h_active = use_x_timing ? X_H_ACTIVE_PIXELS : MODE_H_ACTIVE_PIXELS;

    // Per-scanline command lists.
    vblank_line_vsync_off[0] = HSTX_CMD_RAW_REPEAT | h_front;
    vblank_line_vsync_off[1] = SYNC_V1_H1;
    vblank_line_vsync_off[2] = HSTX_CMD_RAW_REPEAT | h_sync;
    vblank_line_vsync_off[3] = SYNC_V1_H0;
    vblank_line_vsync_off[4] = HSTX_CMD_RAW_REPEAT | (h_back + h_active);
    vblank_line_vsync_off[5] = SYNC_V1_H1;
    vblank_line_vsync_off[6] = HSTX_CMD_NOP;

    vblank_line_vsync_on[0] = HSTX_CMD_RAW_REPEAT | h_front;
    vblank_line_vsync_on[1] = SYNC_V0_H1;
    vblank_line_vsync_on[2] = HSTX_CMD_RAW_REPEAT | h_sync;
    vblank_line_vsync_on[3] = SYNC_V0_H0;
    vblank_line_vsync_on[4] = HSTX_CMD_RAW_REPEAT | (h_back + h_active);
    vblank_line_vsync_on[5] = SYNC_V0_H1;
    vblank_line_vsync_on[6] = HSTX_CMD_NOP;

    vactive_line[0] = HSTX_CMD_RAW_REPEAT | h_front;
    vactive_line[1] = SYNC_V1_H1;
    vactive_line[2] = HSTX_CMD_NOP;
    vactive_line[3] = HSTX_CMD_RAW_REPEAT | h_sync;
    vactive_line[4] = SYNC_V1_H0;
    vactive_line[5] = HSTX_CMD_NOP;
    vactive_line[6] = HSTX_CMD_RAW_REPEAT | h_back;
    vactive_line[7] = SYNC_V1_H1;
    vactive_line[8] = HSTX_CMD_TMDS | h_active;

    if (hdmi_native || hdmi_rgb121) {
        // RGB332 (8bpp) byte = RRRGGGBB. NBITS field is (bits - 1). RGB1024 scans a
        // core1-filled RGB332 line buffer, so it uses this same expander config.
        hstx_ctrl_hw->expand_tmds =
            0u << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB | 2u << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |  // red   [7:5]
            29u << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB | 2u << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB | // green [4:2]
            26u << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB | 1u << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB;  // blue  [1:0]
        // Four 8-bit pixels per 32-bit word.
        hstx_ctrl_hw->expand_shift =
            4u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            8u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
    } else {
        // RGB565 word = RRRRRGGGGGGBBBBB: L0=blue[4:0], L1=green[10:5], L2=red[15:11].
        hstx_ctrl_hw->expand_tmds =
            29u << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB | 4u << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            3u << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB | 5u << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            8u << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB | 4u << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB;
        // Two 16-bit pixels per 32-bit word.
        hstx_ctrl_hw->expand_shift =
            2u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            16u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;
    }

    // Serial: clock period 5 cycles, pop command expander every 5, shift 2/cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // Clock lane pair (base bit + inverted neighbour).
    const int clk_bit = MICROPY_HW_HDMI_CLK;
    hstx_ctrl_hw->bit[clk_bit] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[(clk_bit & 1) ? clk_bit - 1 : clk_bit + 1] =
        HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;

    const int lane_to_bit[3] = { MICROPY_HW_HDMI_D0, MICROPY_HW_HDMI_D1, MICROPY_HW_HDMI_D2 };
    for (uint lane = 0; lane < 3; ++lane) {
        int bit = lane_to_bit[lane];
        uint32_t sel = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit] = sel;
        hstx_ctrl_hw->bit[(bit & 1) ? bit - 1 : bit + 1] = sel | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, 0); // HSTX
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_slew_rate(i, GPIO_SLEW_RATE_FAST);
        gpio_set_input_enabled(i, false);
        gpio_set_pulls(i, false, false);
        gpio_set_input_hysteresis_enabled(i, false);
    }

    // Ping-pong DMA: each channel sends one scanline then chains to the other.
    dma_channel_config c = dma_channel_get_default_config(dmach_ping);
    channel_config_set_chain_to(&c, dmach_pong);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(dmach_ping, &c, &hstx_fifo_hw->fifo,
        vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    c = dma_channel_get_default_config(dmach_pong);
    channel_config_set_chain_to(&c, dmach_ping);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(dmach_pong, &c, &hstx_fifo_hw->fifo,
        vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    // Use DMA IRQ line 1: line 0 already has MicroPython's shared rp2.DMA
    // handler (rp2_dma_init), so an exclusive handler there would hard-assert.
    dma_hw->ints1 = (1u << dmach_ping) | (1u << dmach_pong);
    dma_hw->inte1 = (1u << dmach_ping) | (1u << dmach_pong);
    hdmi_installed_irq = (hdmi_mode == HDMI_MODE_RGB1024) ? hdmi_dma_irq_1024
        : (hdmi_mode == HDMI_MODE_RGB512) ? hdmi_dma_irq_x
        : hdmi_dma_irq;
    irq_set_exclusive_handler(DMA_IRQ_1, hdmi_installed_irq);
    irq_set_enabled(DMA_IRQ_1, true); // enabled on core1 -> ISR runs on core1

    dma_channel_start(dmach_ping);

    hdmi_fill_loop();
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
                row[i] = (uint8_t)((hi << 4) | (lo & 0x0f));
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

// Switch clk_sys for the requested HDMI clock and re-time everything that
// derives from it. Mirrors machine.freq()'s proven sequence (flash timing,
// set_sys_clock, UART baud, PSRAM QMI re-time) and adds the cyw43 gSPI PIO
// divider. Runs on core0 while core1 (the scanout) is stopped. The GC heap
// lives in PSRAM, so psram_init() must re-run after the PLL moves.
static void hdmi_set_clock(uint32_t khz) {
    int old_freq = clock_get_hz(clk_sys);
    int freq = (int)(khz * 1000);
    if (freq == old_freq) {
        return;
    }
    // Whole switch runs with interrupts masked (like MMBasic): the PLL park is
    // brief, and it guarantees the cyw43 gSPI SM is idle when we retune it below
    // (no transfer mid-flight) and that nothing touches PSRAM/flash while their
    // timing is momentarily stale.
    uint32_t irqs = save_and_disable_interrupts();
    // Raise the flash divider (conservative for whichever clock is faster)
    // before the switch, so XIP is never briefly overclocked.
    rp2_flash_set_timing_for_freq(freq > old_freq ? freq : old_freq);
    bool ok = set_sys_clock_khz(khz, false);
    if (ok) {
        if (freq < old_freq) {
            rp2_flash_set_timing_for_freq(freq);
        }
        #if MICROPY_HW_ENABLE_UART_REPL
        setup_default_uart();
        mp_uart_init(); // clk_peri follows clk_sys, so re-derive the REPL baud
        #endif
        #if MICROPY_HW_ENABLE_PSRAM
        psram_init(MICROPY_HW_PSRAM_CS_PIN); // re-time the PSRAM QMI window (heap!)
        #endif
        #if CYW43_PIO_CLOCK_DIV_DYNAMIC
        extern void cyw43_set_pio_clkdiv_int_frac8(uint32_t clock_div_int, uint8_t clock_div_frac8);
        uint32_t d = (khz + 99999) / 100000; // MMBasic algorithm; SCK = clk_sys/(2*d)
        if (d < 2) {
            d = 2;
        }
        cyw43_set_pio_clkdiv_int_frac8(d, 0); // stored value (applied at next bus init)
        #if MICROPY_PY_NETWORK_CYW43 || MICROPY_PY_BLUETOOTH_CYW43
        // Retune the LIVE gSPI SM if Wi-Fi is up, else the link desyncs at the
        // new clk_sys (SCK out of range). The SDK only applies the divider in
        // cyw43_spi_init(), so we set it on the running SM and restart it.
        hdmi_cyw43_bus_t *bd = (hdmi_cyw43_bus_t *)((cyw43_int_t *)&cyw43_state)->bus_data;
        if (bd != NULL && bd->pio != NULL) {
            pio_sm_set_clkdiv_int_frac8(bd->pio, bd->pio_sm, d, 0);
            pio_sm_clkdiv_restart(bd->pio, bd->pio_sm);
        }
        #endif
        #endif
    }
    restore_interrupts(irqs);
    if (!ok) {
        mp_raise_ValueError(MP_ERROR_TEXT("cannot set clock"));
    }
}

// --- Python bindings -------------------------------------------------------
static mp_obj_t hdmi_init(size_t n_args, const mp_obj_t *args) {
    if (hdmi_running) {
        return mp_const_none;
    }
    int mode = (n_args > 0) ? mp_obj_get_int(args[0]) : HDMI_MODE_RGB640;
    if (mode != HDMI_MODE_RGB640 && mode != HDMI_MODE_RGB320 &&
        mode != HDMI_MODE_RGB512 && mode != HDMI_MODE_RGB1024) {
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
    hdmi_set_clock(hdmi_clock_khz); // before core1 launches (reads the new clk_sys)
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
    } else { // RGB1024
        hdmi_w = 1024;
        hdmi_h = 600;
        hdmi_native = 0;
        hdmi_rgb121 = 1;
        hdmi_transfer_count = X_H_ACTIVE_PIXELS / 4;   // 256 words (native 1024-wide RGB332 line)
        hdmi_pal_ensure();    // load the default palette on first use
        hdmi_pal_rebuild();   // (re)build the SRAM expansion table from the live palette
    }
    if (dmach_ping < 0) {
        dmach_ping = dma_claim_unused_channel(true);
        dmach_pong = dma_claim_unused_channel(true);
    }
    // A mode change invalidates every FRAMEBUFFER-style target (buffer sizes
    // differ per mode): drop the layer, release the F buffer (the GC reclaims
    // it), and point drawing back at the display — as MMBasic's mode switch.
    hdmi_layer_on = 0;
    hdmi_target = HDMI_TARGET_N;
    MP_STATE_PORT(hdmi_framebuf_f) = NULL;
    memset(hdmi_fb, 0, sizeof(hdmi_fb)); // clear to black (0 = black in both formats)
    hdmi_gen++;                          // signal the console to resync/home
    v_scanline = 2;
    dma_pong = false;
    vactive_cmdlist_posted = false;
    hdmi_running = true;
    hdmi_core1_stack[0] = HDMI_STACK_SENTINEL;
    // Owns core1 with its own stack; not a lockout victim, so core0 flash writes
    // never pause the scanout.
    multicore_launch_core1_with_stack(hdmi_core1_entry, hdmi_core1_stack, sizeof(hdmi_core1_stack));
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
    return mp_obj_new_bool(hdmi_core1_stack[0] == HDMI_STACK_SENTINEL);
}
static MP_DEFINE_CONST_FUN_OBJ_0(hdmi_stack_ok_obj, hdmi_stack_ok);

// Stop the scanout so a new mode can be started with init(). Halts core1, aborts
// the chained DMA cleanly, and stops HSTX. The claimed DMA channels are kept.
static mp_obj_t hdmi_deinit(void) {
    if (!hdmi_running) {
        return mp_const_none;
    }
    hdmi_running = false;
    __sev(); // wake core1 if idling in __wfe

    // Halt core1 so its DMA IRQ / fill-loop stops touching the hardware.
    multicore_reset_core1();

    // Break both DMA chains first (point chain_to at self, via the
    // non-triggering al1_ctrl alias) so aborting one can't re-trigger the other,
    // then abort both together. Bounded waits so a stuck channel can't hang.
    hw_write_masked(&dma_hw->ch[dmach_ping].al1_ctrl,
        (uint32_t)dmach_ping << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB, DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    hw_write_masked(&dma_hw->ch[dmach_pong].al1_ctrl,
        (uint32_t)dmach_pong << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB, DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    __dmb();
    dma_hw->abort = (1u << dmach_ping) | (1u << dmach_pong);
    uint64_t dl = time_us_64() + 2000;
    while ((dma_hw->abort & ((1u << dmach_ping) | (1u << dmach_pong))) && time_us_64() < dl) {
        tight_loop_contents();
    }
    while ((dma_hw->ch[dmach_ping].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) && time_us_64() < dl) {
        tight_loop_contents();
    }
    while ((dma_hw->ch[dmach_pong].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) && time_us_64() < dl) {
        tight_loop_contents();
    }

    dma_hw->inte1 &= ~((1u << dmach_ping) | (1u << dmach_pong));
    dma_hw->ints1 = (1u << dmach_ping) | (1u << dmach_pong);
    hstx_ctrl_hw->csr = 0; // stop HSTX output
    // Remove the DMA_IRQ_1 handler so a later init() can install a different one
    // (RGB512 uses hdmi_dma_irq_x): irq_set_exclusive_handler hard-asserts if a
    // *different* handler is already registered.
    if (hdmi_installed_irq) {
        irq_remove_handler(DMA_IRQ_1, hdmi_installed_irq);
        hdmi_installed_irq = NULL;
    }
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
static mp_obj_t hdmi_scroll(size_t n_args, const mp_obj_t *args) {
    int rows = mp_obj_get_int(args[0]);
    mp_int_t colour = (n_args > 1) ? mp_obj_get_int(args[1]) : 0;
    if (rows <= 0 || rows >= hdmi_h) {
        return mp_const_none;
    }
    uint8_t *buf = hdmi_wbuf();
    int stride = hdmi_rgb121 ? (hdmi_w / 2) : (hdmi_w * ((hdmi_native) ? 1 : 2));
    int keep = hdmi_h - rows;
    memmove(buf, buf + (size_t)rows * stride, (size_t)keep * stride);
    uint8_t *bottom = buf + (size_t)keep * stride;
    if (hdmi_rgb121) {
        uint8_t c = (uint8_t)(colour & 0x0f);
        memset(bottom, (uint8_t)((c << 4) | c), (size_t)rows * stride);
    } else if (hdmi_native) {
        memset(bottom, (int)(colour & 0xFF), (size_t)rows * stride);
    } else {
        uint16_t *p = (uint16_t *)bottom;
        int count = rows * hdmi_w;
        uint16_t c = (uint16_t)colour;
        for (int i = 0; i < count; i++) {
            p[i] = c;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_scroll_obj, 1, 2, hdmi_scroll);

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
                    *pb = (x & 1) ? ((*pb & 0x0f) | (uint8_t)(v << 4)) : ((*pb & 0xf0) | v);
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

// Blit one 8x12 glyph at pixel (px,py) scaled by `scale` (each font pixel -> a
// scale x scale block). fg/bg are native-format colours; a negative `bg` means
// a transparent background (only the set pixels are drawn). Format-aware, writes
// hdmi_fb directly, clipped to the framebuffer.
static void hdmi_blit_glyph(int px, int py, int ch, mp_int_t fg, mp_int_t bg, int scale) {
    if (ch < FONT_FIRST || ch > 0xFF) {
        ch = FONT_FIRST;
    }
    const uint8_t *glyph = &font1[4 + (ch - FONT_FIRST) * FONT_H];
    bool transparent = (bg < 0);
    uint8_t *buf = hdmi_wbuf();
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int sy = 0; sy < scale; sy++) {
            int y = py + row * scale + sy;
            if (y < 0 || y >= hdmi_h) {
                continue;
            }
            for (int col = 0; col < FONT_W; col++) {
                bool on = bits & (0x80 >> col);
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
                        *pb = (x & 1) ? ((*pb & 0x0f) | (uint8_t)(v << 4)) : ((*pb & 0xf0) | v);
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

// hdmi.text(s, x, y, fg[, bg=-1[, scale=1]]) -- draw a string in the 8x12 console
// font at pixel (x,y). bg<0 (default) is transparent; scale enlarges the glyphs.
// Returns the x pixel just past the string (so calls can be chained).
static mp_obj_t hdmi_text(size_t n_args, const mp_obj_t *args) {
    size_t len;
    const char *s = mp_obj_str_get_data(args[0], &len);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    mp_int_t fg = mp_obj_get_int(args[3]);
    mp_int_t bg = (n_args > 4) ? mp_obj_get_int(args[4]) : -1;
    int scale = (n_args > 5) ? mp_obj_get_int(args[5]) : 1;
    if (scale < 1) {
        scale = 1;
    }
    int adv = FONT_W * scale;
    for (size_t i = 0; i < len; i++) {
        hdmi_blit_glyph(x, y, (uint8_t)s[i], fg, bg, scale);
        x += adv;
    }
    return mp_obj_new_int(x);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_text_obj, 4, 6, hdmi_text);

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
    uint8_t *p = m_malloc(hdmi_fb_bytes()); // raises MemoryError if exhausted
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

// --- Blitter (MMBasic BLIT, generalised to any src/dst target) -------------

// Read/write one pixel of a mode-format buffer. Small helpers with a mode
// branch: the opaque byte-addressable fast path below never uses them; they
// serve the 4bpp-packed and skip-colour paths (sprite-sized blits).
static inline mp_int_t hdmi_px_get(const uint8_t *b, int x, int y) {
    if (hdmi_rgb121) {
        uint8_t v = b[(size_t)y * (hdmi_w / 2) + (x >> 1)];
        return (x & 1) ? (v >> 4) : (v & 0x0f);
    }
    if (hdmi_native) {
        return b[(size_t)y * hdmi_w + x];
    }
    return ((const uint16_t *)b)[(size_t)y * hdmi_w + x];
}
static inline void hdmi_px_set(uint8_t *b, int x, int y, mp_int_t v) {
    if (hdmi_rgb121) {
        uint8_t *p = &b[(size_t)y * (hdmi_w / 2) + (x >> 1)];
        *p = (x & 1) ? ((*p & 0x0f) | (uint8_t)((v & 0x0f) << 4))
                     : ((*p & 0xf0) | (uint8_t)(v & 0x0f));
    } else if (hdmi_native) {
        b[(size_t)y * hdmi_w + x] = (uint8_t)v;
    } else {
        ((uint16_t *)b)[(size_t)y * hdmi_w + x] = (uint16_t)v;
    }
}

// hdmi.blit(x, y, w, h, x1, y1 [, src [, dst [, skip]]]) -- copy the w x h
// rectangle at (x,y) of `src` to (x1,y1) of `dst`. src/dst are target letters
// ("N"/"L"/"F", default: the current write target for both), so it blits
// within one buffer or between any two. `skip` is a native-format colour that
// is NOT copied (source pixels of that colour leave the destination alone,
// -1/default = copy everything). Clipping follows MMBasic's BLIT: a rectangle
// partly off either buffer is trimmed on both sides in step. Overlapping
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
    uint8_t *s = (n_args > 6 && args[6] != mp_const_none)
        ? hdmi_target_ptr_checked(hdmi_parse_target(args[6])) : hdmi_wbuf();
    uint8_t *d = (n_args > 7 && args[7] != mp_const_none)
        ? hdmi_target_ptr_checked(hdmi_parse_target(args[7])) : hdmi_wbuf();
    mp_int_t skip = (n_args > 8) ? mp_obj_get_int(args[8]) : -1;
    if (w < 1 || h < 1) {
        return mp_const_none;
    }
    // Clip both rectangles in step (MMBasic cmd_blit, verbatim shape): a
    // negative source origin shifts the destination (and vice versa), then
    // both are clamped to the buffer geometry.
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
    if (x + w > hdmi_w) {
        w = hdmi_w - x;
    }
    if (x1 + w > hdmi_w) {
        w = hdmi_w - x1;
    }
    if (y + h > hdmi_h) {
        h = hdmi_h - y;
    }
    if (y1 + h > hdmi_h) {
        h = hdmi_h - y1;
    }
    if (w < 1 || h < 1 || x < 0 || x + w > hdmi_w || x1 < 0 || x1 + w > hdmi_w
        || y < 0 || y + h > hdmi_h || y1 < 0 || y1 + h > hdmi_h) {
        return mp_const_none;
    }
    bool overlap = (s == d);
    if (overlap && x == x1 && y == y1) {
        return mp_const_none;
    }

    if (skip < 0 && !hdmi_rgb121) {
        // Opaque, byte-addressable (8/16bpp): one memmove per row (memmove
        // covers horizontal overlap); iterate bottom-up when the destination
        // is below the source so vertical overlap is safe too.
        int bpp = hdmi_native ? 1 : 2;
        size_t stride = (size_t)hdmi_w * bpp;
        size_t nbytes = (size_t)w * bpp;
        if (overlap && y1 > y) {
            for (int j = h - 1; j >= 0; j--) {
                memmove(d + (size_t)(y1 + j) * stride + (size_t)x1 * bpp,
                    s + (size_t)(y + j) * stride + (size_t)x * bpp, nbytes);
            }
        } else {
            for (int j = 0; j < h; j++) {
                memmove(d + (size_t)(y1 + j) * stride + (size_t)x1 * bpp,
                    s + (size_t)(y + j) * stride + (size_t)x * bpp, nbytes);
            }
        }
        return mp_const_none;
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
            mp_int_t v = hdmi_px_get(s, x + i, y + j);
            if (v != skip) {
                hdmi_px_set(d, x1 + i, y1 + j, v);
            }
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(hdmi_blit_obj, 6, 9, hdmi_blit);

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
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&hdmi_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&hdmi_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy), MP_ROM_PTR(&hdmi_copy_obj) },
    { MP_ROM_QSTR(MP_QSTR_blit), MP_ROM_PTR(&hdmi_blit_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&hdmi_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_RGB640), MP_ROM_INT(HDMI_MODE_RGB640) },
    { MP_ROM_QSTR(MP_QSTR_RGB320), MP_ROM_INT(HDMI_MODE_RGB320) },
    { MP_ROM_QSTR(MP_QSTR_RGB512), MP_ROM_INT(HDMI_MODE_RGB512) },
    { MP_ROM_QSTR(MP_QSTR_RGB1024), MP_ROM_INT(HDMI_MODE_RGB1024) },
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
