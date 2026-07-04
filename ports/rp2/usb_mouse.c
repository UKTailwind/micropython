/*
 * USB mouse support for the Pico Computer 3.
 *
 * Ported from MMBasic (PicoMite):
 *   - analyze_mouse_descriptor / process_mouse_report (USBKeyboard.c): detect the
 *     mouse type (8/12/16-bit X/Y) from the report descriptor and decode each
 *     input report into button/x/y/wheel deltas.
 *   - process_mouse_input (KeyboardMap.c): accumulate the deltas into a virtual
 *     cursor position clamped to the screen, track buttons + wheel + double-click.
 *
 * Exposed the same way as MMBasic's DEVICE(MOUSE n, "...") reader, via the
 * `mouse` Python module (usb_mouse_mod.c) which calls usb_mouse_query().
 * Single active mouse (the touch/keyboard model), on its HID slot.
 */

#include "py/mpconfig.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "tusb.h"
#include "pico/time.h"
#include "usb_mouse.h"

// Current framebuffer geometry (0 before hdmi.init()), from hdmi.c.
extern int hdmi_get_width(void);
extern int hdmi_get_height(void);

// --- Mouse type + descriptor info (from MMBasic Hardware_Includes.h) ---------
typedef enum {
    MOUSE_TYPE_UNKNOWN = 0,
    MOUSE_TYPE_STANDARD_8BIT = 1, // 4 bytes: buttons, X, Y, wheel
    MOUSE_TYPE_HIGHRES_12BIT = 2, // 5 bytes: buttons, 12-bit X/Y packed, wheel
    MOUSE_TYPE_GAMING_16BIT = 3,  // 6+ bytes: buttons, 16-bit X, 16-bit Y, wheel
} mouse_report_type_t;

typedef struct {
    mouse_report_type_t type;
    uint8_t report_length;
    uint8_t x_bits;
    uint8_t y_bits;
    uint8_t button_count;
    bool has_wheel;
    bool has_pan;
    uint8_t wheel_byte_offset;
    bool uses_report_id;
    uint8_t report_id;
} mouse_info_t;

// Report layouts for the high-res / gaming mice (standard 8-bit uses TinyUSB's
// hid_mouse_report_t). Field order matches MMBasic.
typedef struct TU_ATTR_PACKED {
    uint8_t buttons;
    uint8_t data[3];
    int8_t wheel;
    int8_t pan;
} hid_mouse_report_12bit_t;

typedef struct TU_ATTR_PACKED {
    uint8_t buttons;
    uint8_t data[4];
    int8_t wheel;
    int8_t pan;
} hid_gaming_mouse_report_t;

// --- Published mouse state (read by usb_mouse_query) -------------------------
static uint8_t mouse_addr = 0xFF, mouse_inst = 0xFF;
static bool mouse_present = false;
static int mouse_slot_num = 0; // 1-based HID slot
static mouse_info_t mouse_info;
static float mouse_speed = 1.0f; // Option.mousespeed: raw delta divided by this

static volatile int mouse_ax = 0, mouse_ay = 0; // accumulated position (screen coords)
static volatile int mouse_az = 0;               // wheel accumulator
static volatile int mouse_l = 0, mouse_r = 0, mouse_c = 0; // buttons
static volatile int mouse_buttons = 0;          // raw button bitmap (bits L/R/M)
static volatile int mouse_dclick = 0;           // double-click (clear on read)

// --- Descriptor parser (MMBasic analyze_mouse_descriptor) --------------------
static mouse_report_type_t analyze_mouse_descriptor(const uint8_t *desc, uint16_t len, mouse_info_t *info) {
    if (!desc || !info || len == 0) {
        return MOUSE_TYPE_UNKNOWN;
    }
    memset(info, 0, sizeof(*info));

    uint8_t report_size = 0, report_count = 0;
    bool found_x = false, found_y = false;
    uint8_t x_bits = 0, y_bits = 0, button_count = 0;
    uint8_t bit_position = 0;

    for (uint16_t i = 0; i < len;) {
        uint8_t bSize = desc[i] & 0x03;
        uint8_t bType = (desc[i] >> 2) & 0x03;
        uint8_t bTag = (desc[i] >> 4) & 0x0F;
        i++;
        uint32_t data = 0;
        for (int j = 0; j < bSize; j++) {
            if (i + j < len) {
                data |= (desc[i + j] << (j * 8));
            }
        }
        i += bSize;

        if (bType == 1 && bTag == 8) { // Global Report ID
            info->uses_report_id = true;
            info->report_id = data;
        }
        if (bType == 1) {
            if (bTag == 7) {
                report_size = data;
            } else if (bTag == 9) {
                report_count = data;
            }
        } else if (bType == 2) {
            if (bTag == 0) { // Usage
                if (data == 0x30) {
                    found_x = true;
                } else if (data == 0x31) {
                    found_y = true;
                } else if (data == 0x38) {
                    info->has_wheel = true;
                    info->wheel_byte_offset = bit_position / 8;
                } else if (data == 0x3C) {
                    info->has_pan = true;
                }
            } else if (bTag == 2) { // Usage Maximum -> button count
                if (data >= 0x01 && data <= 0x20) {
                    button_count = data;
                }
            }
        } else if (bType == 0) {
            if (bTag == 8) { // Input
                if (found_x && !x_bits) {
                    x_bits = report_size;
                    found_x = false;
                }
                if (found_y && !y_bits) {
                    y_bits = report_size;
                    found_y = false;
                }
                bit_position += (report_size * report_count);
            }
        }
    }

    info->x_bits = x_bits;
    info->y_bits = y_bits;
    info->button_count = button_count;
    info->report_length = (bit_position + 7) / 8;
    if (info->uses_report_id) {
        info->report_length += 1;
    }

    if (x_bits == 8 && y_bits == 8) {
        info->type = MOUSE_TYPE_STANDARD_8BIT;
    } else if (x_bits == 12 && y_bits == 12) {
        info->type = MOUSE_TYPE_HIGHRES_12BIT;
    } else if (x_bits == 16 && y_bits == 16) {
        info->type = MOUSE_TYPE_GAMING_16BIT;
    } else {
        info->type = MOUSE_TYPE_UNKNOWN;
    }
    return info->type;
}

// --- Accumulate a decoded report (MMBasic process_mouse_input) ---------------
static void mouse_accumulate(int x_delta, int y_delta, int wheel_delta, uint8_t buttons) {
    // Left-button double-click: two presses within 500 ms.
    static uint64_t leftpress = 0;
    static uint8_t leftstate = 0;
    uint64_t now = time_us_64();
    if (now - leftpress > 500000) {
        leftstate = 0;
    }
    if (leftstate == 0 && (buttons & MOUSE_BUTTON_LEFT)) {
        leftpress = now;
        leftstate = 1;
    } else if (leftstate == 1 && !(buttons & MOUSE_BUTTON_LEFT)) {
        leftpress = now;
        leftstate = 2;
    } else if (leftstate == 2 && (buttons & MOUSE_BUTTON_LEFT)) {
        leftpress = now;
        leftstate = 3;
        mouse_dclick = 1;
    }

    mouse_l = (buttons & MOUSE_BUTTON_LEFT) ? 1 : 0;
    mouse_r = (buttons & MOUSE_BUTTON_RIGHT) ? 1 : 0;
    mouse_c = (buttons & MOUSE_BUTTON_MIDDLE) ? 1 : 0;
    mouse_buttons = buttons & 0x07;

    int x_max = (hdmi_get_width() > 0) ? hdmi_get_width() : 1024;
    int y_max = (hdmi_get_height() > 0) ? hdmi_get_height() : 1024;

    // /2 matches MMBasic's fixed post-scale (on top of Option.mousespeed).
    int ax = mouse_ax + x_delta / 2;
    if (ax >= x_max) {
        ax = x_max - 1;
    }
    if (ax < 0) {
        ax = 0;
    }
    mouse_ax = ax;

    int ay = mouse_ay + y_delta / 2;
    if (ay >= y_max) {
        ay = y_max - 1;
    }
    if (ay < 0) {
        ay = 0;
    }
    mouse_ay = ay;

    mouse_az += wheel_delta;
}

// --- Public API — called from the mp_usbh.c HID callbacks -------------------
void usb_mouse_mount(uint8_t dev_addr, uint8_t instance,
    const uint8_t *desc_report, uint16_t desc_len) {
    mouse_addr = dev_addr;
    mouse_inst = instance;
    mouse_present = true;
    analyze_mouse_descriptor(desc_report, desc_len, &mouse_info);
    if (mouse_info.type == MOUSE_TYPE_UNKNOWN) {
        mouse_info.type = MOUSE_TYPE_STANDARD_8BIT; // best-effort default
    }
    // Start the cursor centred on the current screen so it's visible.
    int w = hdmi_get_width(), h = hdmi_get_height();
    mouse_ax = (w > 0) ? w / 2 : 0;
    mouse_ay = (h > 0) ? h / 2 : 0;
    mouse_az = 0;
    mouse_l = mouse_r = mouse_c = mouse_buttons = mouse_dclick = 0;
}

bool usb_mouse_owns(uint8_t dev_addr, uint8_t instance) {
    return mouse_present && dev_addr == mouse_addr && instance == mouse_inst;
}

void usb_mouse_on_report(uint8_t dev_addr, uint8_t instance,
    const uint8_t *report, uint16_t len) {
    if (!usb_mouse_owns(dev_addr, instance) || !report) {
        return;
    }
    // Skip a leading report-id byte if the descriptor declared one.
    if (mouse_info.uses_report_id) {
        report++;
        if (len) {
            len--;
        }
    }
    float sp = (mouse_speed == 0.0f) ? 1.0f : mouse_speed;
    int x_delta = 0, y_delta = 0, wheel = 0;
    uint8_t buttons = 0;

    switch (mouse_info.type) {
        case MOUSE_TYPE_HIGHRES_12BIT: {
            const hid_mouse_report_12bit_t *r = (const hid_mouse_report_12bit_t *)report;
            buttons = r->buttons;
            int16_t x12 = r->data[0] | ((r->data[1] & 0x0F) << 8);
            if (x12 & 0x0800) {
                x12 |= 0xF000;
            }
            int16_t y12 = ((r->data[1] & 0xF0) >> 4) | (r->data[2] << 4);
            if (y12 & 0x0800) {
                y12 |= 0xF000;
            }
            x_delta = (int)(x12 / sp);
            y_delta = (int)(y12 / sp);
            wheel = r->wheel;
            break;
        }
        case MOUSE_TYPE_GAMING_16BIT: {
            const hid_gaming_mouse_report_t *r = (const hid_gaming_mouse_report_t *)report;
            buttons = r->buttons;
            int16_t x16 = r->data[0] | (r->data[1] << 8);
            int16_t y16 = r->data[2] | (r->data[3] << 8);
            x_delta = (int)(x16 / sp);
            y_delta = (int)(y16 / sp);
            wheel = r->wheel;
            break;
        }
        case MOUSE_TYPE_STANDARD_8BIT:
        default: {
            const hid_mouse_report_t *r = (const hid_mouse_report_t *)report;
            buttons = r->buttons;
            x_delta = (int)(r->x / sp);
            y_delta = (int)(r->y / sp);
            wheel = r->wheel;
            break;
        }
    }
    mouse_accumulate(x_delta, y_delta, wheel, buttons);
}

void usb_mouse_on_umount(uint8_t dev_addr, uint8_t instance) {
    if (!usb_mouse_owns(dev_addr, instance)) {
        return;
    }
    mouse_present = false;
    mouse_addr = mouse_inst = 0xFF;
    mouse_slot_num = 0;
}

void usb_mouse_set_slot(int slot) {
    mouse_slot_num = slot;
}

void usb_mouse_set_speed(float v) {
    mouse_speed = v;
}
float usb_mouse_get_speed(void) {
    return mouse_speed;
}

// --- Query — read state for the `mouse` module (no USB access) --------------
int usb_mouse_query(int code) {
    switch (code) {
        case MQ_X: return mouse_ax;
        case MQ_Y: return mouse_ay;
        case MQ_L: return mouse_l;
        case MQ_R: return mouse_r;
        case MQ_M: return mouse_c;
        case MQ_W: return mouse_az;
        case MQ_B: return mouse_buttons;
        case MQ_D: { int v = mouse_dclick; mouse_dclick = 0; return v; }
        case MQ_T: return mouse_info.has_wheel ? 3 : 0;
        case MQ_PRESENT: return mouse_present ? 1 : 0;
        case MQ_SLOT: return mouse_slot_num;
        default: return -1;
    }
}

#endif // MICROPY_HW_USB_HOST
