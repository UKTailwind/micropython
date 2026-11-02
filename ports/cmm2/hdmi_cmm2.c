/*
 * The hdmi module's display on the Colour Maximite 2 G2: the back end of the Pico Computer 3's hdmi.c (ports/rp2, used
 * unchanged), as hdmi_rp2.c is on the PC3 and hdmi_sdl.c in its emulator (hdmi_priv.h has the contract).
 *
 * Each mode is shown on the CMM2 mode of its size, set through the loader's services (version 6):
 *   RGB640    640x480 RGB332      MODE 8,8    hdmi_fb copied as it is
 *   RGB320_8  320x240 RGB332      MODE 7,8    each row twice (the CMM2's page for a doubled mode holds each row twice)
 *   RGB320    320x240 RGB565      MODE 7,16   each row twice
 *   RGB512    512x300 RGB565      MODE 18,16 at half size (2x2 pixels, so 1024x600 on the monitor), each row twice
 *   RGB640_4  640x480 RGB121      MODE 8,8    each byte (two pixels) made two RGB332 bytes through hdmi_map256
 *   RGB1024   1024x600 RGB121     MODE 18,8   the same
 * The 8-bit modes' palette is RGB332 (the emulator's map332), so their pixels are RGB332 as on the PC3.
 *
 * The PC3 scans hdmi_fb out continuously; here the loader's frame hook puts it on the shown page at the start of each
 * frame's blanking, in PendSV. The plain copies are the DMA2D's (dma2d_copy), so they cost the program nothing but the
 * SDRAM they share; the layer's merge (RGB320 and RGB320_8, while hdmi.layer() is on) and the 16-colour modes'
 * expansion are made by the CPU there. Either keeps ahead of the LTDC, which starts on row 0 as blanking ends.
 *
 * in_blanking() is true from when a frame's picture has been taken (the hook done, and its copies) until 1.5 ms before
 * the next frame, so hdmi.vsync() returns as a picture has been taken: the program then has the rest of the frame to
 * draw the next. The frame's period is measured between the hooks (75 Hz in modes 7 and 8, 60 in 18).
 *
 * While the display is on, the console is not drawn on the screen (con_screen(0)): MMBasic's text would be overwritten
 * at the next frame. The PC3's own console (pcconsole) draws the REPL into hdmi_fb.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <string.h>
#include "py/runtime.h"

#if MICROPY_HW_ENABLE_HDMI

#include "../rp2/hdmi_priv.h"
#include "cmm2.h"

#define BLANK_END_US 1500                   // in_blanking() false from this long before the next frame

static cmm2_video video;                   // the CMM2 mode's page 0
static volatile uint32_t frame_seq;        // the frames' hooks run (written last by the hook)
static volatile uint32_t frame_start;      // when the last one ran (micros' low 32 bits)
static volatile uint32_t frame_period;     // between the last two
static volatile int frame_dma;             // the last frame's picture is the DMA2D's copies

// RGB332 as the PC3's monitor shows it (hdmi_sdl.c's map332)
static void palette332(void) {
    static uint8_t rgb[256 * 3];
    for (int i = 0; i < 256; i++) {
        int r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        rgb[i * 3] = r * 255 / 7;
        rgb[i * 3 + 1] = g * 255 / 7;
        rgb[i * 3 + 2] = b * 255 / 3;
    }
    cmm2->video_palette(0, 256, rgb);
}

// the 16-colour modes: each source byte (left pixel in its high nibble) two RGB332 bytes, four bytes at a time
static void expand121(void) {
    const uint32_t *s = (const uint32_t *)hdmi_fb;
    uint32_t *d = (uint32_t *)video.fb;
    uint32_t n = (uint32_t)hdmi_w * hdmi_h / 8;
    while (n--) {
        uint32_t w = *s++;
        *d++ = hdmi_map256[w & 0xFF] | (uint32_t)hdmi_map256[(w >> 8) & 0xFF] << 16;
        *d++ = hdmi_map256[(w >> 16) & 0xFF] | (uint32_t)hdmi_map256[w >> 24] << 16;
    }
}

// RGB320_8 and RGB320 with the layer on: a layer pixel wins unless it is the transparent colour; each row twice
static void merge8(void) {
    const uint8_t *s = hdmi_fb, *l = hdmi_fb + hdmi_fb_bytes();
    uint8_t t = (uint8_t)hdmi_layer_transp;
    for (int y = 0; y < hdmi_h; y++) {
        uint8_t *d0 = video.fb + y * video.pitch, *d1 = d0 + hdmi_w;
        for (int x = 0; x < hdmi_w; x++) {
            uint8_t v = l[x] != t ? l[x] : s[x];
            d0[x] = v;
            d1[x] = v;
        }
        s += hdmi_w;
        l += hdmi_w;
    }
}
static void merge16(void) {
    const uint16_t *s = (const uint16_t *)hdmi_fb, *l = (const uint16_t *)(hdmi_fb + hdmi_fb_bytes());
    uint16_t t = hdmi_layer_transp;
    for (int y = 0; y < hdmi_h; y++) {
        uint16_t *d0 = (uint16_t *)(video.fb + y * video.pitch), *d1 = d0 + hdmi_w;
        for (int x = 0; x < hdmi_w; x++) {
            uint16_t v = l[x] != t ? l[x] : s[x];
            d0[x] = v;
            d1[x] = v;
        }
        s += hdmi_w;
        l += hdmi_w;
    }
}

// the frame hook (PendSV): hdmi_fb onto the page
static void frame(void) {
    uint32_t now = (uint32_t)cmm2->micros(), d = now - frame_start;
    if (d > 5000 && d < 30000) {
        frame_period = d;
    }
    frame_start = now;
    frame_seq++;
    frame_dma = 0;
    if (hdmi_rgb121) {
        expand121();
    } else if (hdmi_layer_on && hdmi_mode == HDMI_MODE_RGB320_8) {
        merge8();
    } else if (hdmi_layer_on && hdmi_mode == HDMI_MODE_RGB320) {
        merge16();
    } else {
        uint32_t row = hdmi_w * (hdmi_native ? 1 : 2);
        frame_dma = 1;
        if (cmm2->dma2d_busy()) {
            return;                         // (last frame's not done: this one is skipped)
        }
        for (uint32_t c = 0; c < video.copies; c++) {
            cmm2->dma2d_copy(hdmi_fb, row, video.fb + c * row, video.pitch, row, hdmi_h);
        }
    }
}

void hdmi_backend_start(void) {
    int mode, depth = 8, scale = 1;
    cmm2->frame_hook(NULL);
    while (cmm2->dma2d_busy()) {
    }
    switch (hdmi_mode) {
        case HDMI_MODE_RGB320_8:
            mode = 7;
            break;
        case HDMI_MODE_RGB320:
            mode = 7;
            depth = 16;
            break;
        case HDMI_MODE_RGB512:
            mode = 18;
            depth = 16;
            scale = 2;
            break;
        case HDMI_MODE_RGB1024:
            mode = 18;
            break;
        default:                            // RGB640, RGB640_4
            mode = 8;
            break;
    }
    if (cmm2->video_scaled(mode, depth, scale, &video) < 0) {
        hdmi_running = false;
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("the CMM2 can't show this mode"));
    }
    if (depth == 8) {
        palette332();
    }
    cmm2->con_screen(0);
    frame_dma = 0;
    frame_period = 13333;                   // (till it is measured)
    frame_start = (uint32_t)cmm2->micros();
    cmm2->frame_hook(frame);
}

void hdmi_backend_stop(void) {
    cmm2->frame_hook(NULL);
    while (cmm2->dma2d_busy()) {
    }
    cmm2->con_screen(1);
}

void hdmi_backend_set_clock(uint32_t khz) {
    (void)khz;                              // (the PC3's system clock: the CMM2's modes have their own pixel clocks)
}

bool hdmi_backend_in_blanking(void) {
    uint32_t seq = frame_seq, start = frame_start;
    int dma = frame_dma;
    if (seq != frame_seq) {
        return false;                       // (a frame began as these were read)
    }
    if (dma && cmm2->dma2d_busy()) {
        return false;                       // its picture not yet taken
    }
    return (uint32_t)cmm2->micros() - start < frame_period - BLANK_END_US;
}

bool hdmi_backend_stack_ok(void) {
    return true;
}

#endif // MICROPY_HW_ENABLE_HDMI
