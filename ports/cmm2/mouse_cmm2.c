/*
 * The mouse on the Colour Maximite 2 G2: the back end of the Pico Computer 3's mouse module (ports/rp2's
 * usb_mouse_mod.c, used unchanged), as mouse_sdl.c is in its emulator. The mouse is the firmware's (MM.MOUSE's: USB,
 * PS/2 or I2C), its position kept by it in the screen's pixels, which while the hdmi module's display is on are
 * hdmi_w x hdmi_h (hdmi_cmm2.c sets the CMM2 mode of that size). A double click (query("D")) is a left press within
 * 500 ms of the one before, seen by cmm2_mouse_poll() from the VM's hook.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/runtime.h"

#if MICROPY_HW_USB_HOST

#include "../rp2/usb_mouse.h"
#include "../rp2/hdmi_priv.h"
#include "cmm2.h"

#define DCLICK_US 500000

static int m_dclick;                        // a double click, cleared on reading it
static int m_left;                          // the left button as last seen
static uint64_t m_press;                    // when it was last pressed
static float m_speed = 1.0f;

void cmm2_mouse_poll(void) {
    int m[4];
    if (!cmm2->mouse(m)) {
        m_left = 0;
        return;
    }
    if ((m[2] & 1) && !m_left) {
        uint64_t now = cmm2->micros();
        if (m_press && now - m_press < DCLICK_US) {
            m_dclick = 1;
            m_press = 0;
        } else {
            m_press = now;
        }
    }
    m_left = m[2] & 1;
}

int usb_mouse_query(int code) {
    int m[4], present = cmm2->mouse(m);
    switch (code) {
        case MQ_X:
            return m[0] >= hdmi_w ? hdmi_w - 1 : m[0];
        case MQ_Y:
            return m[1] >= hdmi_h ? hdmi_h - 1 : m[1];
        case MQ_L:
            return (m[2] & 1) ? 1 : 0;
        case MQ_R:
            return (m[2] & 2) ? 1 : 0;
        case MQ_M:
            return (m[2] & 4) ? 1 : 0;
        case MQ_W:
            return m[3];
        case MQ_B:
            return m[2];
        case MQ_D: {
            int d = m_dclick;
            m_dclick = 0;
            return d;
        }
        case MQ_T:
            return present ? 3 : 0;         // (the firmware does not say whether it has a wheel)
        case MQ_PRESENT:
            return present;
        case MQ_SLOT:
            return present;
        default:
            return 0;
    }
}

void usb_mouse_set_speed(float v) {
    if (v > 0) {
        m_speed = v;                        // (kept for the API: the firmware moves the pointer)
    }
}

float usb_mouse_get_speed(void) {
    return m_speed;
}

#endif // MICROPY_HW_USB_HOST
