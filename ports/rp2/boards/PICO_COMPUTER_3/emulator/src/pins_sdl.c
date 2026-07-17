/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * (Licence text as per ports/rp2/hdmi.c.)
 */

// The virtual I/O header: a second SDL window ("import Pins" opens it) with
// a switch and an LED for every pin the Pico Computer 3 exposes, and a
// potentiometer on each analog pin (GP40-46, the RP2350B ADC pins on the
// header). Switches drive machine.Pin inputs (and Pin.irq via Pins.py's
// dispatcher); LEDs show Pin outputs; pots feed machine.ADC.read_u16().
//
// The window lives on the display's SDL thread (pc3emu_pins_frame/_event,
// called from hdmi_sdl.c's loop); the _emupins module is the VM-side state
// API. State words are single volatile ints -- benign cross-thread reads.

#include <string.h>
#include <stdio.h>

#include "py/runtime.h"

#if MICROPY_HW_ENABLE_HDMI

#include <SDL.h>

// font1 (8x12, first char 0x20, [3]=count): defined in console_font.h,
// already compiled into the build via hdmi.c's font set.
extern const unsigned char font1[];

// The exposed pins, as on the machine's I/O header silkscreen.
typedef struct {
    uint8_t gpio;
    bool analog;
} pindef_t;
static const pindef_t pindefs[] = {
    {0, false}, {1, false}, {2, false}, {3, false},
    {4, false}, {5, false}, {6, false}, {7, false},
    {20, false}, {21, false}, {26, false},
    {34, false}, {35, false}, {36, false}, {37, false}, {38, false}, {39, false},
    {40, true}, {41, true}, {42, true}, {43, true}, {44, true}, {45, true}, {46, true},
};
#define NPINS (sizeof(pindefs) / sizeof(pindefs[0]))

// --- shared state -----------------------------------------------------------
static volatile uint8_t sw_state[48];   // latched input switches
static volatile uint8_t sw_touched[48]; // user has clicked it (idle_set won't override)
static volatile uint8_t pin_isout[48];  // pin configured as OUTPUT
static volatile uint8_t pin_outval[48]; // last written output value
static volatile uint16_t pot_val[48];   // ADC pot positions
static volatile bool panel_want = false;

// Switch-change ring (SDL thread produces, Pins.py's dispatcher consumes).
static volatile uint8_t chg[64][2];
static volatile int chg_head, chg_tail;

static void push_change(int gpio, int v) {
    int next = (chg_head + 1) % 64;
    if (next != chg_tail) {
        chg[chg_head][0] = (uint8_t)gpio;
        chg[chg_head][1] = (uint8_t)v;
        chg_head = next;
    }
}

// --- panel UI (SDL thread only) ---------------------------------------------
#define ROWS_PER_COL 12
#define ROW_H  27
#define COL_W  320
#define PAD    10
#define PANEL_W (2 * COL_W + 3 * PAD)
#define PANEL_H (ROWS_PER_COL * ROW_H + 2 * PAD + 16)

static SDL_Window *pw = NULL;
static SDL_Renderer *pr = NULL;
static SDL_Texture *pt = NULL;
static uint32_t pbuf[PANEL_W * PANEL_H];
static int drag_pin = -1; // pot being dragged

#define C_BG    0xFF202428u
#define C_TEXT  0xFFD0D4D8u
#define C_DIM   0xFF60666Cu
#define C_SW_ON 0xFF40C040u
#define C_SW_OFF 0xFF404850u
#define C_LED_ON 0xFFFF4040u
#define C_LED_OFF 0xFF502020u
#define C_TRACK 0xFF404850u
#define C_KNOB  0xFF80B0FFu

static void p_rect(int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i >= 0 && i < PANEL_W && j >= 0 && j < PANEL_H) {
                pbuf[j * PANEL_W + i] = c;
            }
        }
    }
}

static void p_disc(int cx, int cy, int r, uint32_t c) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                int i = cx + dx, j = cy + dy;
                if (i >= 0 && i < PANEL_W && j >= 0 && j < PANEL_H) {
                    pbuf[j * PANEL_W + i] = c;
                }
            }
        }
    }
}

static void p_text(int x, int y, const char *s, uint32_t c) {
    for (; *s; s++, x += 8) {
        unsigned ch = (unsigned char)*s;
        if (ch < 0x20 || ch >= 0x20 + font1[3]) {
            continue;
        }
        const unsigned char *g = &font1[4 + (ch - 0x20) * 12];
        for (int row = 0; row < 12; row++) {
            for (int bit = 0; bit < 8; bit++) {
                if (g[row] & (0x80 >> bit)) {
                    int i = x + bit, j = y + row;
                    if (i >= 0 && i < PANEL_W && j >= 0 && j < PANEL_H) {
                        pbuf[j * PANEL_W + i] = c;
                    }
                }
            }
        }
    }
}

// Row geometry: label | switch | LED | (slider + value on analog rows)
#define SW_X   52
#define SW_W   34
#define SW_H   16
#define LED_X  108
#define SLI_X  132
#define SLI_W  120

static void row_origin(size_t idx, int *x, int *y) {
    *x = PAD + (idx / ROWS_PER_COL) * (COL_W + PAD);
    *y = PAD + 14 + (idx % ROWS_PER_COL) * ROW_H;
}

static void panel_render(void) {
    for (size_t i = 0; i < PANEL_W * PANEL_H; i++) {
        pbuf[i] = C_BG;
    }
    p_text(PAD, PAD - 2, "I/O header  (click switches, drag pots)", C_DIM);
    char lab[16];
    for (size_t i = 0; i < NPINS; i++) {
        int x, y;
        row_origin(i, &x, &y);
        int g = pindefs[i].gpio;
        snprintf(lab, sizeof(lab), "GP%d", g);
        p_text(x, y + 2, lab, C_TEXT);
        // switch: a latched pushbutton
        bool on = sw_state[g];
        p_rect(x + SW_X, y, SW_W, SW_H, on ? C_SW_ON : C_SW_OFF);
        p_text(x + SW_X + (on ? 19 : 4), y + 2, on ? "1" : "0", C_TEXT);
        // LED: output value when the pin is an output, else the input level
        bool lit = pin_isout[g] ? pin_outval[g] : sw_state[g];
        p_disc(x + LED_X, y + SW_H / 2, 7, lit ? C_LED_ON : C_LED_OFF);
        if (pindefs[i].analog) {
            int sy = y + SW_H / 2;
            p_rect(x + SLI_X, sy - 2, SLI_W, 4, C_TRACK);
            int kx = x + SLI_X + (int)((uint32_t)pot_val[g] * (SLI_W - 6) / 65535);
            p_rect(kx, sy - 7, 6, 14, C_KNOB);
            snprintf(lab, sizeof(lab), "%5u", pot_val[g]);
            p_text(x + SLI_X + SLI_W + 6, y + 2, lab, C_DIM);
        }
    }
    void *pixels;
    int pitch;
    if (SDL_LockTexture(pt, NULL, &pixels, &pitch) == 0) {
        memcpy(pixels, pbuf, sizeof(pbuf));
        SDL_UnlockTexture(pt);
        SDL_RenderCopy(pr, pt, NULL, NULL);
        SDL_RenderPresent(pr);
    }
}

// Called every frame from the display thread: open/close/redraw the panel.
void pc3emu_pins_frame(void) {
    if (panel_want && pw == NULL) {
        pw = SDL_CreateWindow("Pico Computer 3 -- I/O header",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            PANEL_W, PANEL_H, SDL_WINDOW_ALLOW_HIGHDPI);
        pr = pw ? SDL_CreateRenderer(pw, -1, 0) : NULL;
        pt = pr ? SDL_CreateTexture(pr, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, PANEL_W, PANEL_H) : NULL;
        if (pt == NULL) {
            panel_want = false;
        }
    }
    if (!panel_want && pw != NULL) {
        SDL_DestroyTexture(pt);
        SDL_DestroyRenderer(pr);
        SDL_DestroyWindow(pw);
        pt = NULL;
        pr = NULL;
        pw = NULL;
        drag_pin = -1;
    }
    if (pw != NULL) {
        panel_render();
    }
}

static int hit_pin(int mx, int my, bool *on_switch, bool *on_slider) {
    for (size_t i = 0; i < NPINS; i++) {
        int x, y;
        row_origin(i, &x, &y);
        if (my < y - 4 || my > y + SW_H + 4) {
            continue;
        }
        if (mx >= x + SW_X && mx <= x + SW_X + SW_W) {
            *on_switch = true;
            *on_slider = false;
            return pindefs[i].gpio;
        }
        if (pindefs[i].analog && mx >= x + SLI_X - 4 && mx <= x + SLI_X + SLI_W + 4) {
            *on_switch = false;
            *on_slider = true;
            return pindefs[i].gpio;
        }
    }
    return -1;
}

static void slider_set(int gpio, int mx) {
    for (size_t i = 0; i < NPINS; i++) {
        if (pindefs[i].gpio == gpio) {
            int x, y;
            row_origin(i, &x, &y);
            int rel = mx - (x + SLI_X);
            if (rel < 0) {
                rel = 0;
            }
            if (rel > SLI_W) {
                rel = SLI_W;
            }
            pot_val[gpio] = (uint16_t)((uint32_t)rel * 65535 / SLI_W);
            return;
        }
    }
}

// Event filter for the display thread: returns true when the event belonged
// to the panel window (and was handled here).
bool pc3emu_pins_event(const SDL_Event *ev) {
    if (pw == NULL) {
        return false;
    }
    Uint32 pid = SDL_GetWindowID(pw);
    if (ev->type == SDL_WINDOWEVENT && ev->window.windowID == pid) {
        if (ev->window.event == SDL_WINDOWEVENT_CLOSE) {
            panel_want = false;
        }
        return true;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.windowID == pid) {
        bool on_sw = false, on_sli = false;
        int g = hit_pin(ev->button.x, ev->button.y, &on_sw, &on_sli);
        if (g >= 0 && on_sw) {
            sw_state[g] = !sw_state[g];
            sw_touched[g] = 1;
            push_change(g, sw_state[g]);
        } else if (g >= 0 && on_sli) {
            drag_pin = g;
            slider_set(g, ev->button.x);
        }
        return true;
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.windowID == pid) {
        drag_pin = -1;
        return true;
    }
    if (ev->type == SDL_MOUSEMOTION && ev->motion.windowID == pid) {
        if (drag_pin >= 0) {
            slider_set(drag_pin, ev->motion.x);
        }
        return true;
    }
    if ((ev->type == SDL_KEYDOWN || ev->type == SDL_KEYUP)
        && ev->key.windowID == pid) {
        return true; // keys typed at the panel don't reach the machine
    }
    return false;
}

// --- the _emupins module (VM side) ------------------------------------------

static void pots_init_once(void) {
    static bool done = false;
    if (!done) {
        done = true;
        for (size_t i = 0; i < NPINS; i++) {
            pot_val[pindefs[i].gpio] = 32768; // mid-scale, like a centred knob
        }
    }
}

static mp_obj_t emupins_show(void) {
    pots_init_once();
    panel_want = true;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(emupins_show_obj, emupins_show);

static mp_obj_t emupins_hide(void) {
    panel_want = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(emupins_hide_obj, emupins_hide);

static mp_obj_t emupins_switch_get(mp_obj_t g_in) {
    return MP_OBJ_NEW_SMALL_INT(sw_state[mp_obj_get_int(g_in) & 47]);
}
static MP_DEFINE_CONST_FUN_OBJ_1(emupins_switch_get_obj, emupins_switch_get);

// set_switch(gpio, v) -- programmatic press (tests; scripted rigs).
static mp_obj_t emupins_set_switch(mp_obj_t g_in, mp_obj_t v_in) {
    int g = mp_obj_get_int(g_in) & 47;
    int v = mp_obj_is_true(v_in) ? 1 : 0;
    if (sw_state[g] != v) {
        sw_state[g] = (uint8_t)v;
        sw_touched[g] = 1;
        push_change(g, v);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(emupins_set_switch_obj, emupins_set_switch);

static mp_obj_t emupins_pot_get(mp_obj_t g_in) {
    pots_init_once();
    return MP_OBJ_NEW_SMALL_INT(pot_val[mp_obj_get_int(g_in) & 47]);
}
static MP_DEFINE_CONST_FUN_OBJ_1(emupins_pot_get_obj, emupins_pot_get);

static mp_obj_t emupins_set_output(mp_obj_t g_in, mp_obj_t is_out_in) {
    pin_isout[mp_obj_get_int(g_in) & 47] = mp_obj_is_true(is_out_in);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(emupins_set_output_obj, emupins_set_output);

static mp_obj_t emupins_led_set(mp_obj_t g_in, mp_obj_t v_in) {
    pin_outval[mp_obj_get_int(g_in) & 47] = mp_obj_is_true(v_in);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(emupins_led_set_obj, emupins_led_set);

// idle_set(gpio, v) -- the pull-derived idle level; respected until the
// user has clicked that switch.
static mp_obj_t emupins_idle_set(mp_obj_t g_in, mp_obj_t v_in) {
    int g = mp_obj_get_int(g_in) & 47;
    if (!sw_touched[g]) {
        sw_state[g] = mp_obj_is_true(v_in) ? 1 : 0;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(emupins_idle_set_obj, emupins_idle_set);

static mp_obj_t emupins_next_change(void) {
    if (chg_tail == chg_head) {
        return mp_const_none;
    }
    mp_obj_t t[2] = {
        MP_OBJ_NEW_SMALL_INT(chg[chg_tail][0]),
        MP_OBJ_NEW_SMALL_INT(chg[chg_tail][1]),
    };
    chg_tail = (chg_tail + 1) % 64;
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(emupins_next_change_obj, emupins_next_change);

static const mp_rom_map_elem_t emupins_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__emupins) },
    { MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&emupins_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_hide), MP_ROM_PTR(&emupins_hide_obj) },
    { MP_ROM_QSTR(MP_QSTR_switch_get), MP_ROM_PTR(&emupins_switch_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_switch), MP_ROM_PTR(&emupins_set_switch_obj) },
    { MP_ROM_QSTR(MP_QSTR_pot_get), MP_ROM_PTR(&emupins_pot_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_output), MP_ROM_PTR(&emupins_set_output_obj) },
    { MP_ROM_QSTR(MP_QSTR_led_set), MP_ROM_PTR(&emupins_led_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_idle_set), MP_ROM_PTR(&emupins_idle_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_next_change), MP_ROM_PTR(&emupins_next_change_obj) },
};
static MP_DEFINE_CONST_DICT(emupins_module_globals, emupins_module_globals_table);

const mp_obj_module_t emupins_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&emupins_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__emupins, emupins_module);

#endif // MICROPY_HW_ENABLE_HDMI
