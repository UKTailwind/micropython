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

// The RP2350 scanout backend for hdmi.c: HSTX DVI signal generation on
// core1 (ping-pong DMA + per-mode IRQs + fill loop) plus the clk_sys
// switcher. Moved VERBATIM from hdmi.c behind the hdmi_backend_* seam
// (hdmi_priv.h) so the drawing core can be shared with the PC emulator.
// No behaviour change intended.

#include <string.h>

#include "py/runtime.h"

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
#include "hardware/psram.h" // psram_configure_params / psram_reinitialize
#include "clocks_extra.h"  // set_core_voltage_for_khz

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

#include "hdmi_priv.h"

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

// --- Scanout state (all SRAM) ----------------------------------------------
static uint16_t HDMIlines[2][HDMI_MAX_H]; // RGB565 doubled scanlines (up to 1024)
static uint32_t vblank_line_vsync_off[7];
static uint32_t vblank_line_vsync_on[7];
static uint32_t vactive_line[9];

static volatile int32_t v_scanline = 2;
static volatile bool dma_pong = false;
static volatile bool vactive_cmdlist_posted = false;
static int dmach_ping = -1, dmach_pong = -1;
static irq_handler_t hdmi_installed_irq = NULL; // current DMA_IRQ_1 handler (for removal on deinit)

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
        if (hdmi_mode == HDMI_MODE_RGB640) {
            // Native 640x480x8: scan the framebuffer line directly.
            ch->read_addr = (uintptr_t)&hdmi_fb[active * MODE_H_ACTIVE_PIXELS];
        } else {
            // Everything else at this timing (RGB320/RGB640_4/RGB320_8): the
            // core1 fill-loop has doubled/expanded this line.
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

// --- core1 fill loop: per-mode line preparation into the next line buffer -
// (RGB640 is scanned directly from the framebuffer, so core1 idles.)
static void __not_in_flash_func(hdmi_fill_loop)(void) {
    if (hdmi_mode == HDMI_MODE_RGB640) {
        while (hdmi_running) { // RGB640 scans the framebuffer directly; core1 idles
            __wfe();
        }
        return;
    }
    if (hdmi_mode == HDMI_MODE_RGB320_8) {
        // 320x240 RGB332 -> 640x480: double 320 source bytes to 640, vertical
        // double. With the layer enabled, merge per pixel first: the layer
        // byte wins unless it equals the transparent colour (verbatim
        // MMBasic's SCREENMODE5 merge in HDMI.c). Layer = second quarter of
        // the static video memory, so every read here stays SRAM.
        const uint8_t *layer8 = hdmi_fb + 320 * 240;
        int last_line8 = 2;
        while (hdmi_running) {
            if (v_scanline != last_line8) {
                last_line8 = v_scanline;
                int active = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
                uint8_t *p = (uint8_t *)HDMIlines[last_line8 & 1];
                if (active >= 0 && active < MODE_V_ACTIVE_LINES) {
                    const uint8_t *s = &hdmi_fb[(active >> 1) * 320];
                    if (hdmi_layer_on) {
                        const uint8_t *l = &layer8[(active >> 1) * 320];
                        uint8_t t = (uint8_t)hdmi_layer_transp;
                        for (int i = 0; i < 320; i++) {
                            uint8_t v = l[i];
                            if (v == t) {
                                v = s[i];
                            }
                            *p++ = v;
                            *p++ = v;
                        }
                    } else {
                        for (int i = 0; i < 320; i++) {
                            uint8_t v = s[i];
                            *p++ = v;
                            *p++ = v;
                        }
                    }
                }
            }
        }
        return;
    }
    if (hdmi_rgb121) {
        // 4bpp native (RGB1024 at 1024x600, RGB640_4 at 640x480): expand the
        // packed source row into RGB332 pixels. One SRAM lookup + one 16-bit
        // store per source byte (two pixels), no doubling. hdmi_map256 /
        // hdmi_fb / HDMIlines are all SRAM (no flash reads). The bounds are
        // hoisted out of the loop, so the per-line work stays the hot
        // lookup+store pair for both geometries.
        const uint16_t *m = hdmi_map256;
        const int total = (hdmi_mode == HDMI_MODE_RGB1024) ? X_V_TOTAL_LINES : MODE_V_TOTAL_LINES;
        const int lines = (hdmi_mode == HDMI_MODE_RGB1024) ? X_V_ACTIVE_LINES : MODE_V_ACTIVE_LINES;
        const int half = hdmi_w / 2; // packed bytes per source row
        int last_line = 2;
        while (hdmi_running) {
            if (v_scanline != last_line) {
                last_line = v_scanline;
                int active = v_scanline - (total - lines);
                uint16_t *p = (uint16_t *)HDMIlines[last_line & 1];
                if (active >= 0 && active < lines) {
                    const uint8_t *s = &hdmi_fb[active * half];
                    for (int i = 0; i < half; i++) {
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

// Switch clk_sys for the requested HDMI clock and re-time everything that
// derives from it. Mirrors machine.freq()'s proven sequence (flash timing,
// set_sys_clock, UART baud, PSRAM QMI re-time) and adds the cyw43 gSPI PIO
// divider. Runs on core0 while core1 (the scanout) is stopped. The GC heap
// lives in PSRAM, so its QMI window must be re-timed after the PLL moves.
static void hdmi_set_clock(uint32_t khz) {
    int old_freq = clock_get_hz(clk_sys);
    int freq = (int)(khz * 1000);
    if (freq == old_freq) {
        return;
    }
    // Core voltage goes UP before the clock does, and comes down only after
    // (below): the core must never run at the new speed on the old, lower
    // voltage. Deliberately outside the masked region that follows - it needs
    // 10 ms to settle and that window has to stay short.
    if (freq > old_freq) {
        set_core_voltage_for_khz(khz);
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
        #if MICROPY_HW_ENABLE_PSRAM
        // Re-time the PSRAM QMI window (the GC heap lives there).  Must come
        // before the flash timing below: psram_reinitialize() goes through
        // flash_start_xip(), which restores the bootrom's XIP configuration
        // for CS0 and discards ours.
        if (psram_is_available()) {
            psram_configure_params(PICO_DEFAULT_PSRAM_MAX_FREQ, PICO_DEFAULT_PSRAM_MAX_SELECT, PICO_DEFAULT_PSRAM_MIN_DESELECT);
            psram_reinitialize();
        }
        #endif
        // Now the divider for the new clock, unconditionally: on the way up the
        // conservative one set before the switch is replaced, on the way down the
        // slower clock allows a smaller divisor.
        rp2_flash_set_timing_for_freq(freq);
        #if MICROPY_HW_ENABLE_UART_REPL
        setup_default_uart();
        mp_uart_init(); // clk_peri follows clk_sys, so re-derive the REPL baud
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
    // Coming down: drop the voltage only once the core is already slow, and
    // only if the clock actually moved.
    if (ok && freq < old_freq) {
        set_core_voltage_for_khz(khz);
    }
    if (!ok) {
        mp_raise_ValueError(MP_ERROR_TEXT("cannot set clock"));
    }
}

// --- The backend seam (hdmi_priv.h) -----------------------------------------

void hdmi_backend_set_clock(uint32_t khz) {
    hdmi_set_clock(khz);
}

void hdmi_backend_start(void) {
    if (dmach_ping < 0) {
        dmach_ping = dma_claim_unused_channel(true);
        dmach_pong = dma_claim_unused_channel(true);
    }
    v_scanline = 2;
    dma_pong = false;
    vactive_cmdlist_posted = false;
    hdmi_core1_stack[0] = HDMI_STACK_SENTINEL;
    // Owns core1 with its own stack; not a lockout victim, so core0 flash
    // writes never pause the scanout.
    multicore_launch_core1_with_stack(hdmi_core1_entry, hdmi_core1_stack, sizeof(hdmi_core1_stack));
}

void hdmi_backend_stop(void) {
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
}

bool hdmi_backend_in_blanking(void) {
    // Group by TIMING, not by mode name: only RGB512/RGB1024 run the 1024x600
    // timing; everything else (RGB640/RGB320/RGB640_4/RGB320_8) is 640x480.
    int blank = (hdmi_mode == HDMI_MODE_RGB512 || hdmi_mode == HDMI_MODE_RGB1024)
        ? X_BLANKING_COUNT : BLANKING_COUNT;
    return v_scanline < blank;
}

bool hdmi_backend_stack_ok(void) {
    return hdmi_core1_stack[0] == HDMI_STACK_SENTINEL;
}

#endif // MICROPY_HW_ENABLE_HDMI
