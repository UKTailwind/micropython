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

// Shared contract between the hdmi module's drawing core (hdmi.c) and its
// scanout backends: hdmi_rp2.c (HSTX DVI generated on core1) on the machine,
// and the PC emulator's SDL backend. The backend owns signal generation and
// frame pacing; hdmi.c owns the buffers, targets, palette and every drawing
// function, so both displays are produced by identical code.
#ifndef MICROPY_INCLUDED_RP2_HDMI_PRIV_H
#define MICROPY_INCLUDED_RP2_HDMI_PRIV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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

// --- Shared state (defined in hdmi.c) --------------------------------------
extern uint8_t hdmi_fb[HDMI_FB_BYTES];
extern volatile bool hdmi_running;
extern int hdmi_mode;
extern int hdmi_w, hdmi_h;                // logical framebuffer dimensions
extern int hdmi_transfer_count;           // active pixel words per line
extern int hdmi_native;                   // 1 = native 8bpp scan (RGB640)
extern int hdmi_rgb121;                   // 1 = 4bpp packed framebuffer
extern uint32_t hdmi_clock_khz;
extern volatile int hdmi_layer_on;        // backend merges the layer when set
extern volatile uint16_t hdmi_layer_transp; // layer transparent colour (RGB565)
extern uint16_t hdmi_map256[256];         // RGB121 byte -> 2 RGB332 px
size_t hdmi_fb_bytes(void);               // bytes in one mode-sized buffer

// --- Backend interface ------------------------------------------------------
// start(): shared state (mode/buffers) is set and hdmi_running is true;
//          claim scanout resources and start refreshing the display.
// stop():  hdmi_running is already false; halt scanout and release hardware.
// set_clock(): rp2 moves clk_sys (may raise); the emulator ignores it.
// in_blanking(): true while the scan is inside vertical blanking -- the
//          shared hdmi.vsync() builds its two-phase wait from this.
// stack_ok(): scanout-engine health (core1 stack sentinel on rp2).
void hdmi_backend_start(void);
void hdmi_backend_stop(void);
void hdmi_backend_set_clock(uint32_t khz);
bool hdmi_backend_in_blanking(void);
bool hdmi_backend_stack_ok(void);

#endif // MICROPY_INCLUDED_RP2_HDMI_PRIV_H
