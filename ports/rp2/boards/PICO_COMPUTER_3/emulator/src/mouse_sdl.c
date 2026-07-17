/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * (Licence text as per ports/rp2/hdmi.c.)
 */

// The PC-emulator mouse: SDL window mouse events feed the same state the
// firmware's `mouse` module (usb_mouse_mod.c, compiled unchanged) queries --
// MMBasic DEVICE(MOUSE) semantics. The window position maps to framebuffer
// coordinates (the window shows the doubled output, the machine's cursor
// lives in framebuffer space), so pccursor/pcgui behave identically.

#include <string.h>

#include "py/runtime.h"

#if MICROPY_HW_USB_HOST

#include "../../../../usb_mouse.h"
#include "../../../../hdmi_priv.h"

// State written by the SDL thread (single words: benign to read racily).
static volatile int m_x, m_y;         // framebuffer coordinates
static volatile int m_buttons;        // MMBasic bitmap: 1 L, 2 R, 4 M
static volatile int m_wheel;          // accumulator
static volatile int m_dclick;         // left double-click, cleared on read
static volatile bool m_seen = false;  // any mouse event yet (PRESENT)
static float m_speed = 1.0f;

// Called from hdmi_sdl.c's event loop (SDL thread). kind: 0 motion (a=x, b=y
// in WINDOW coordinates), 1 button (a=SDL button, b=down, c=clicks),
// 2 wheel (a=delta).
void pc3emu_mouse_sdl_event(int kind, int a, int b, int c, int out_w, int out_h) {
    m_seen = true;
    if (kind == 0) {
        // Window pixels -> framebuffer pixels (RGB320's window is doubled).
        m_x = (out_w > 0) ? a * hdmi_w / out_w : a;
        m_y = (out_h > 0) ? b * hdmi_h / out_h : b;
    } else if (kind == 1) {
        int bit = (a == 1) ? 1 : (a == 3) ? 2 : (a == 2) ? 4 : 0; // SDL L/R/M
        if (b) {
            m_buttons |= bit;
            if (bit == 1 && c >= 2) {
                m_dclick = 1;
            }
        } else {
            m_buttons &= ~bit;
        }
    } else if (kind == 2) {
        m_wheel += a;
    }
}

int usb_mouse_query(int code) {
    switch (code) {
        case MQ_X:
            return m_x;
        case MQ_Y:
            return m_y;
        case MQ_L:
            return (m_buttons & 1) ? 1 : 0;
        case MQ_R:
            return (m_buttons & 2) ? 1 : 0;
        case MQ_M:
            return (m_buttons & 4) ? 1 : 0;
        case MQ_W:
            return m_wheel;
        case MQ_B:
            return m_buttons;
        case MQ_D: {
            int d = m_dclick;
            m_dclick = 0;
            return d;
        }
        case MQ_T:
            return 3; // the PC mouse has a wheel
        case MQ_PRESENT:
            return m_seen ? 1 : 0;
        case MQ_SLOT:
            return m_seen ? 2 : 0; // hardware puts the mouse in slot 2
        default:
            return 0;
    }
}

void usb_mouse_set_speed(float v) {
    if (v > 0) {
        m_speed = v; // absolute positioning: stored for API fidelity only
    }
}

float usb_mouse_get_speed(void) {
    return m_speed;
}

#endif // MICROPY_HW_USB_HOST
