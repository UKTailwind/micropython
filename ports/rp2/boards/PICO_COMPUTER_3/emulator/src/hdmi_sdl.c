/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * (Licence text as per ports/rp2/hdmi.c.)
 */

// The PC-emulator scanout backend for hdmi.c (contract: hdmi_priv.h).
//
// A host thread plays the role of core1 + HSTX: every ~16.7 ms it composites
// the visible image from the SAME shared state the hardware scans -- hdmi_fb
// in the active mode's pixel format, the overlay layer merged per pixel with
// the transparent colour (RGB320, exactly the fill-loop's rule), the RGB121
// palette through the same hdmi_map256 expansion table -- and presents it in
// an SDL window at the mode's true output resolution (640x480 or 1024x600),
// pixel-doubling just as the hardware does. hdmi.vsync() gets hardware
// semantics from in_blank: a short "blanking" window opens right after each
// present.
//
// One deliberate difference: the hardware scans the live framebuffer, so a
// mid-frame write can tear on the monitor; the emulator snapshots once per
// frame, so tearing never shows. The book teaches draw-after-vsync, which
// behaves identically in both.

#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "py/runtime.h"
#include "py/mphal.h"

#if MICROPY_HW_ENABLE_HDMI

#include <SDL.h>

#include "../../../../hdmi_priv.h" // ports/rp2/hdmi_priv.h (no -I on ports/rp2 -- see micropython.mk)

static pthread_t sdl_thread;
static volatile bool sdl_thread_up = false;   // window exists, loop running
static volatile bool sdl_want_stop = false;
static volatile bool sdl_failed = false;      // SDL init failed (headless?)
static volatile bool in_blank = false;

// Colour expansion tables: what the monitor shows for each framebuffer value.
static uint32_t map332[256];       // RGB332 -> ARGB8888
static uint32_t *map565 = NULL;    // RGB565 -> ARGB8888 (256 KB, built once)

static void build_maps(void) {
    for (int i = 0; i < 256; i++) {
        int r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        map332[i] = 0xFF000000u | ((uint32_t)(r * 255 / 7) << 16)
            | ((uint32_t)(g * 255 / 7) << 8) | (uint32_t)(b * 255 / 3);
    }
    if (map565 == NULL) {
        map565 = malloc(65536 * sizeof(uint32_t));
        for (int i = 0; i < 65536; i++) {
            int r = (i >> 11) & 31, g = (i >> 5) & 63, b = i & 31;
            map565[i] = 0xFF000000u | ((uint32_t)(r * 255 / 31) << 16)
                | ((uint32_t)(g * 255 / 63) << 8) | (uint32_t)(b * 255 / 31);
        }
    }
}

// Composite the frame into an out_w*out_h ARGB8888 buffer -- the exact
// mode/doubling/layer rules of hdmi_rp2.c's DMA scan + fill loop.
static void compose(uint32_t *dst, int out_w, int out_h) {
    if (hdmi_native) {
        // RGB640: native 8bpp RGB332 scan.
        const uint8_t *s = hdmi_fb;
        for (int i = 0; i < out_w * out_h; i++) {
            dst[i] = map332[s[i]];
        }
    } else if (hdmi_rgb121) {
        // RGB1024: 4bpp packed through hdmi_map256 (palette) to RGB332, native res.
        for (int y = 0; y < out_h; y++) {
            const uint8_t *s = &hdmi_fb[y * (out_w / 2)];
            uint32_t *p = &dst[y * out_w];
            for (int i = 0; i < out_w / 2; i++) {
                uint16_t two = hdmi_map256[s[i]];
                *p++ = map332[two & 0xFF];
                *p++ = map332[two >> 8];
            }
        }
    } else {
        // RGB320 / RGB512: RGB565, pixel+line doubled. RGB320 merges the layer
        // first (layer pixel wins unless it equals the transparent colour).
        const uint16_t *fb16 = (const uint16_t *)hdmi_fb;
        const uint16_t *layer16 = (const uint16_t *)(hdmi_fb + hdmi_fb_bytes());
        bool merge = hdmi_layer_on && hdmi_mode == HDMI_MODE_RGB320;
        uint16_t t = hdmi_layer_transp;
        for (int sy = 0; sy < hdmi_h; sy++) {
            const uint16_t *s = &fb16[sy * hdmi_w];
            const uint16_t *l = &layer16[sy * hdmi_w];
            uint32_t *p0 = &dst[(sy * 2) * out_w];
            uint32_t *p1 = &dst[(sy * 2 + 1) * out_w];
            for (int sx = 0; sx < hdmi_w; sx++) {
                uint16_t v = merge ? (l[sx] != t ? l[sx] : s[sx]) : s[sx];
                uint32_t c = map565[v];
                *p0++ = c;
                *p0++ = c;
                *p1++ = c;
                *p1++ = c;
            }
        }
    }
}

static void *sdl_thread_main(void *arg) {
    (void)arg;
    int out_w = (hdmi_mode == HDMI_MODE_RGB512 || hdmi_mode == HDMI_MODE_RGB1024) ? 1024 : 640;
    int out_h = (out_w == 1024) ? 600 : 480;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sdl_failed = true;
        sdl_thread_up = true; // unblock start()
        return NULL;
    }
    SDL_Window *win = SDL_CreateWindow("Pico Computer 3",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        out_w, out_h, SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (ren == NULL && win != NULL) {
        ren = SDL_CreateRenderer(win, -1, 0); // software fallback
    }
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, out_w, out_h) : NULL;
    if (tex == NULL) {
        sdl_failed = true;
        sdl_thread_up = true;
        if (ren) {
            SDL_DestroyRenderer(ren);
        }
        if (win) {
            SDL_DestroyWindow(win);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return NULL;
    }
    build_maps();
    sdl_thread_up = true;

    while (!sdl_want_stop) {
        // Active-display period: the program draws.
        in_blank = false;
        SDL_Delay(15);

        // Vertical blanking: snapshot and show the finished frame.
        void *pixels;
        int pitch;
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
            compose((uint32_t *)pixels, out_w, out_h);
            SDL_UnlockTexture(tex);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
        }
        in_blank = true;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                mp_sched_keyboard_interrupt(); // window close == Ctrl-C
            }
            // Phase 2b: keyboard events feed the keymap tables here.
        }
        SDL_Delay(1);
        in_blank = false;
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return NULL;
}

// --- The backend seam (hdmi_priv.h) -----------------------------------------

void hdmi_backend_set_clock(uint32_t khz) {
    (void)khz; // validated by shared code; the PC's refresh is host-driven
}

void hdmi_backend_start(void) {
    sdl_want_stop = false;
    sdl_thread_up = false;
    sdl_failed = false;
    pthread_create(&sdl_thread, NULL, sdl_thread_main, NULL);
    while (!sdl_thread_up) {
        usleep(1000); // wait for the window so init() returns with a display
    }
    if (sdl_failed) {
        pthread_join(sdl_thread, NULL);
        hdmi_running = false;
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no display (SDL init failed)"));
    }
}

void hdmi_backend_stop(void) {
    if (sdl_thread_up && !sdl_failed) {
        sdl_want_stop = true;
        pthread_join(sdl_thread, NULL);
    }
    sdl_thread_up = false;
    in_blank = false;
}

bool hdmi_backend_in_blanking(void) {
    return in_blank;
}

bool hdmi_backend_stack_ok(void) {
    return sdl_thread_up && !sdl_failed;
}

#endif // MICROPY_HW_ENABLE_HDMI
