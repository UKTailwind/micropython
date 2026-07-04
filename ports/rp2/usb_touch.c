/*
 * USB multi-touch digitizer support for the Pico Computer 3.
 *
 * Ported from MMBasic (PicoMite):
 *   - analyze_touch_descriptor / process_touch_report / touch_reassemble and
 *     the Windows-Precision-Touchscreen digitizer-init handshake from
 *     input/USBKeyboard.c
 *   - the single- and two-finger gesture state machine from graphics/Pointer.c
 *
 * Adapted to MicroPython's rp2 USB host (mp_usbh.c): event-driven HID
 * callbacks instead of MMBasic's HID[] slot poller, and a single active touch
 * panel (the normal case). Contact coordinates are scaled to the current HDMI
 * framebuffer geometry so they match the pixel space the user draws in.
 *
 * This file includes tusb.h, so it is NOT on the QSTR-scan path; the `touch`
 * Python module lives in usb_touch_mod.c and reads state via usb_touch_query().
 */

#include "py/mpconfig.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include <math.h>
#include "tusb.h"
#include "pico/time.h"
#include "usb_touch.h"

// Current framebuffer geometry (0 before hdmi.init()), from hdmi.c.
extern int hdmi_get_width(void);
extern int hdmi_get_height(void);
#define HRes (hdmi_get_width())
#define VRes (hdmi_get_height())

// ============================================================================
// Parsed descriptor + decoded-report types (from MMBasic Hardware_Includes.h)
// ============================================================================
typedef struct {
    bool valid;
    bool uses_report_id;
    uint8_t report_id;
    uint16_t report_length_bytes;
    uint8_t max_contacts;

    uint16_t contact_count_bit_offset;
    uint8_t contact_count_bits;

    uint16_t first_contact_bit_offset;
    uint16_t contact_stride_bits;

    uint8_t tip_switch_bit_offset;
    uint8_t in_range_bit_offset;
    uint8_t contact_id_bit_offset;
    uint8_t contact_id_bits;
    uint8_t x_bit_offset;
    uint8_t x_bits;
    uint8_t y_bit_offset;
    uint8_t y_bits;

    int32_t x_logical_max;
    int32_t y_logical_max;

    bool has_input_mode;
    uint8_t input_mode_report_id;
    uint8_t contact_count_max_report_id;
    uint8_t cert_blob_report_id;

    bool has_pointer_fallback;
    uint8_t pointer_report_id;
    uint16_t pointer_button_bit_offset;
    uint16_t pointer_x_bit_offset;
    uint8_t pointer_x_bits;
    uint16_t pointer_y_bit_offset;
    uint8_t pointer_y_bits;
    int32_t pointer_x_logical_max;
    int32_t pointer_y_logical_max;
    uint16_t pointer_report_length_bytes;
} touch_info_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t id;
    bool tip;
    bool in_range;
} touch_contact_t;

typedef struct {
    uint8_t count;
    touch_contact_t contacts[MAX_TOUCH_CONTACTS];
} touch_report_t;

// ============================================================================
// Published touch state (read by usb_touch_query)
// ============================================================================
static volatile bool usb_touch_present = false;
static volatile bool usb_touch_active = false;
static volatile int16_t usb_touch_x = 0, usb_touch_y = 0;
static volatile uint64_t usb_touch_last_us = 0;
static volatile bool usb_touch_active2 = false;
static volatile int16_t usb_touch_x2 = 0, usb_touch_y2 = 0;
static volatile uint8_t usb_touch_count = 0;
static volatile int16_t usb_touch_xn[MAX_TOUCH_CONTACTS] = {0};
static volatile int16_t usb_touch_yn[MAX_TOUCH_CONTACTS] = {0};

// The single active touch device + its parsed layout and init state.
static uint8_t touch_addr = 0xFF, touch_inst = 0xFF;
static touch_info_t touch_info;
static uint8_t touch_init_step = 0;
static uint8_t touch_feature_buf[256];
static int touch_slot_num = 0; // 1-based HID slot (MMBasic model); 0 = none

// ============================================================================
// Gesture state machine (from MMBasic Pointer.c). Tunables kept identical.
// ============================================================================
static int16_t touch_swipe_start_x = 0, touch_swipe_start_y = 0;
static uint64_t touch_swipe_start_us = 0;
static int touch_swipe_dir = 0;   // 0 none, 1 L, 2 R, 3 U, 4 D
static int touch_tap = 0, touch_longpress = 0, touch_doubletap = 0;
static bool touch_longpress_fired = false;
static uint64_t touch_last_tap_us = 0;
static int16_t touch_last_tap_x = 0, touch_last_tap_y = 0;

#define TAP_MAX_DT 250000ULL
#define HOLD_MIN_DT 500000ULL
#define STILL_MAX 15
#define DBL_TAP_WINDOW 500000ULL
#define DBL_TAP_RADIUS 30
#define SWIPE_MAX_DT 600000ULL

static void touch_gesture_on_down(int16_t x, int16_t y) {
    touch_swipe_start_x = x;
    touch_swipe_start_y = y;
    touch_swipe_start_us = time_us_64();
    touch_swipe_dir = 0;
    touch_longpress_fired = false;
}

static void touch_gesture_tick(int16_t cur_x, int16_t cur_y, bool is_down) {
    if (!is_down || touch_swipe_start_us == 0 || touch_longpress_fired) {
        return;
    }
    uint64_t dt = time_us_64() - touch_swipe_start_us;
    if (dt < HOLD_MIN_DT) {
        return;
    }
    int dx = (int)cur_x - (int)touch_swipe_start_x;
    int dy = (int)cur_y - (int)touch_swipe_start_y;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;
    if (adx > STILL_MAX || ady > STILL_MAX) {
        return; // moved too much — not a hold
    }
    touch_longpress = 1;
    touch_longpress_fired = true;
}

static void touch_gesture_on_up(int16_t end_x, int16_t end_y) {
    if (touch_swipe_start_us == 0) {
        return;
    }
    if (touch_longpress_fired) {
        return; // long-press consumed the gesture
    }
    uint64_t dt = time_us_64() - touch_swipe_start_us;
    int dx = end_x - touch_swipe_start_x;
    int dy = end_y - touch_swipe_start_y;
    touch_swipe_start_us = 0;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;
    int min_dim = (HRes > 0 && VRes > 0) ? (HRes < VRes ? HRes : VRes) : 240;
    int sw_thresh = min_dim / 6;
    if (sw_thresh < 30) {
        sw_thresh = 30;
    }
    if ((adx >= sw_thresh || ady >= sw_thresh) && dt <= SWIPE_MAX_DT) {
        if (adx > ady) {
            touch_swipe_dir = (dx > 0) ? 2 : 1;
        } else {
            touch_swipe_dir = (dy > 0) ? 4 : 3;
        }
        return;
    }
    if (adx > STILL_MAX || ady > STILL_MAX) {
        return;
    }
    if (dt >= HOLD_MIN_DT) {
        touch_longpress = 1;
        return;
    }
    if (dt <= TAP_MAX_DT) {
        uint64_t now = time_us_64();
        if (touch_last_tap_us != 0 && (now - touch_last_tap_us) <= DBL_TAP_WINDOW) {
            int ddx = (int)touch_swipe_start_x - (int)touch_last_tap_x;
            int ddy = (int)touch_swipe_start_y - (int)touch_last_tap_y;
            int addx = (ddx < 0) ? -ddx : ddx;
            int addy = (ddy < 0) ? -ddy : ddy;
            if (addx <= DBL_TAP_RADIUS && addy <= DBL_TAP_RADIUS) {
                touch_doubletap = 1;
                touch_last_tap_us = 0;
                return;
            }
        }
        touch_tap = 1;
        touch_last_tap_us = now;
        touch_last_tap_x = touch_swipe_start_x;
        touch_last_tap_y = touch_swipe_start_y;
    }
}

// --- Two-finger gestures ---------------------------------------------------
static int16_t pinch_start_x1, pinch_start_y1, pinch_start_x2, pinch_start_y2;
static uint64_t pinch_start_us = 0;
static uint32_t touch_pinch_initial_dsq = 0;
static int touch_pinch_dir = 0;   // 0 none, 1 expand, 2 contract
static int touch_rotate_dir = 0;  // 0 none, 1 CW, 2 CCW
static int touch_twotap = 0;

#define ROTATE_RAD_THRESHOLD 0.26f
#define TWOTAP_STILL_MAX 15
#define TWOTAP_MAX_DT 300000ULL

static void touch_gesture_pinch_start(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    pinch_start_x1 = x1;
    pinch_start_y1 = y1;
    pinch_start_x2 = x2;
    pinch_start_y2 = y2;
    pinch_start_us = time_us_64();
    int dx = (int)x2 - (int)x1;
    int dy = (int)y2 - (int)y1;
    touch_pinch_initial_dsq = (uint32_t)(dx * dx + dy * dy);
    touch_pinch_dir = 0;
    touch_rotate_dir = 0;
    touch_twotap = 0;
}

static void touch_gesture_pinch_end(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    if (touch_pinch_initial_dsq == 0) {
        return;
    }
    uint64_t dt = time_us_64() - pinch_start_us;
    int dx_init = (int)pinch_start_x2 - (int)pinch_start_x1;
    int dy_init = (int)pinch_start_y2 - (int)pinch_start_y1;
    int dx_end = (int)x2 - (int)x1;
    int dy_end = (int)y2 - (int)y1;
    uint32_t init_dsq = touch_pinch_initial_dsq;
    uint32_t final_dsq = (uint32_t)(dx_end * dx_end + dy_end * dy_end);
    touch_pinch_initial_dsq = 0;
    uint32_t change_dsq = (final_dsq > init_dsq) ? (final_dsq - init_dsq) : (init_dsq - final_dsq);

    // 1. Pinch (distance ratio change >= 30%).
    if (change_dsq >= 900) {
        if (final_dsq * 100u > init_dsq * 169u) {
            touch_pinch_dir = 1; // expand
            return;
        }
        if (final_dsq * 169u < init_dsq * 100u) {
            touch_pinch_dir = 2; // contract
            return;
        }
    }
    // 2. Rotate (angle between contact vectors >= ~15 deg).
    float cross = (float)dx_init * (float)dy_end - (float)dy_init * (float)dx_end;
    float dot = (float)dx_init * (float)dx_end + (float)dy_init * (float)dy_end;
    if (cross != 0.0f || dot != 0.0f) {
        float angle = atan2f(cross, dot);
        if (angle > ROTATE_RAD_THRESHOLD) {
            touch_rotate_dir = 1; // CW (screen Y down)
            return;
        }
        if (angle < -ROTATE_RAD_THRESHOLD) {
            touch_rotate_dir = 2; // CCW
            return;
        }
    }
    // 3. Two-finger tap (both contacts still + short).
    if (dt <= TWOTAP_MAX_DT) {
        int dx1 = (int)x1 - (int)pinch_start_x1, dy1 = (int)y1 - (int)pinch_start_y1;
        int dx2 = (int)x2 - (int)pinch_start_x2, dy2 = (int)y2 - (int)pinch_start_y2;
        int adx1 = (dx1 < 0) ? -dx1 : dx1, ady1 = (dy1 < 0) ? -dy1 : dy1;
        int adx2 = (dx2 < 0) ? -dx2 : dx2, ady2 = (dy2 < 0) ? -dy2 : dy2;
        if (adx1 <= TWOTAP_STILL_MAX && ady1 <= TWOTAP_STILL_MAX &&
            adx2 <= TWOTAP_STILL_MAX && ady2 <= TWOTAP_STILL_MAX) {
            touch_twotap = 1;
        }
    }
}

// ============================================================================
// HID report-descriptor parser (from MMBasic analyze_touch_descriptor)
// ============================================================================
static uint32_t touch_read_bits(const uint8_t *bytes, uint16_t bit_offset, uint8_t bit_count) {
    uint32_t result = 0;
    for (uint8_t i = 0; i < bit_count; i++) {
        uint16_t b = bit_offset + i;
        if (bytes[b >> 3] & (1u << (b & 7))) {
            result |= (1u << i);
        }
    }
    return result;
}

static bool analyze_touch_descriptor(const uint8_t *desc_report, uint16_t desc_len, touch_info_t *info) {
    if (!desc_report || !info || desc_len == 0) {
        return false;
    }
    memset(info, 0, sizeof(*info));

    uint8_t usage_page = 0;
    uint16_t last_usage = 0;
    uint8_t report_size = 0;
    uint8_t report_count = 0;
    int32_t logical_max = 0;
    uint16_t bit_position = 0;
    int collection_depth = 0;

    int current_report_id = 0;
    int finger_report_id = -1;

    bool in_finger = false;
    int finger_start_depth = -1;
    uint16_t finger_start_bit = 0;
    uint8_t finger_count = 0;
    bool saw_touchscreen_root = false;

    bool in_pointer = false;
    int pointer_start_depth = -1;
    int pointer_report_id = -1;
    bool got_pointer_button = false;
    uint16_t ptr_uq[8];
    uint8_t ptr_uq_n = 0;

    bool in_device_config = false;
    int device_config_depth = -1;

    for (uint16_t i = 0; i < desc_len;) {
        uint8_t prefix = desc_report[i++];
        uint8_t bSize = prefix & 0x03;
        uint8_t bType = (prefix >> 2) & 0x03;
        uint8_t bTag = (prefix >> 4) & 0x0F;

        uint32_t data = 0;
        for (uint8_t j = 0; j < bSize && (i + j) < desc_len; j++) {
            data |= ((uint32_t)desc_report[i + j]) << (j * 8);
        }
        i += bSize;

        if (bType == 1) { // Global
            switch (bTag) {
                case 0: usage_page = data & 0xFF; break;
                case 2: logical_max = (int32_t)data; break;
                case 7: report_size = data & 0xFF; break;
                case 8: // Report ID
                    info->uses_report_id = true;
                    current_report_id = data & 0xFF;
                    bit_position = 0;
                    break;
                case 9: report_count = data & 0xFF; break;
            }
        } else if (bType == 2) { // Local
            if (bTag == 0) {
                last_usage = data & 0xFFFF;
                if (in_pointer && ptr_uq_n < (uint8_t)(sizeof(ptr_uq) / sizeof(ptr_uq[0]))) {
                    ptr_uq[ptr_uq_n++] = last_usage;
                }
            }
        } else if (bType == 0) { // Main
            if (bTag == 10) { // Collection
                collection_depth++;
                if (collection_depth == 1 && usage_page == 0x0D && last_usage == 0x04) {
                    saw_touchscreen_root = true;
                }
                if (collection_depth == 1 && usage_page == 0x01 &&
                    (last_usage == 0x01 || last_usage == 0x02) && !in_pointer) {
                    in_pointer = true;
                    pointer_start_depth = collection_depth;
                }
                if (usage_page == 0x0D && last_usage == 0x0E && !in_device_config) {
                    in_device_config = true;
                    device_config_depth = collection_depth;
                }
                if (usage_page == 0x0D && last_usage == 0x22 && !in_finger) {
                    if (finger_report_id == -1) {
                        finger_report_id = current_report_id;
                        info->report_id = (uint8_t)current_report_id;
                    }
                    if (current_report_id == finger_report_id) {
                        in_finger = true;
                        finger_start_depth = collection_depth;
                        finger_start_bit = bit_position;
                    }
                }
            } else if (bTag == 12) { // End Collection
                if (in_finger && collection_depth == finger_start_depth) {
                    if (finger_count == 0) {
                        info->contact_stride_bits = bit_position - finger_start_bit;
                        info->first_contact_bit_offset = finger_start_bit;
                    }
                    finger_count++;
                    in_finger = false;
                }
                if (in_pointer && collection_depth == pointer_start_depth) {
                    in_pointer = false;
                }
                if (in_device_config && collection_depth == device_config_depth) {
                    in_device_config = false;
                }
                collection_depth--;
            } else if (bTag == 8) { // Input
                uint16_t field_bits = (uint16_t)report_size * report_count;
                bool capture = (finger_report_id == -1) || (current_report_id == finger_report_id);

                if (capture && in_finger && finger_count == 0) {
                    uint16_t rel = bit_position - finger_start_bit;
                    if (usage_page == 0x0D) {
                        if (last_usage == 0x42) {
                            info->tip_switch_bit_offset = rel;
                        } else if (last_usage == 0x32) {
                            info->in_range_bit_offset = rel;
                        } else if (last_usage == 0x51) {
                            info->contact_id_bit_offset = rel;
                            info->contact_id_bits = report_size;
                        }
                    } else if (usage_page == 0x01) {
                        if (last_usage == 0x30) {
                            info->x_bit_offset = rel;
                            info->x_bits = report_size;
                            info->x_logical_max = logical_max;
                        } else if (last_usage == 0x31) {
                            info->y_bit_offset = rel;
                            info->y_bits = report_size;
                            info->y_logical_max = logical_max;
                        }
                    }
                } else if (capture && !in_finger) {
                    if (usage_page == 0x0D && last_usage == 0x54) {
                        info->contact_count_bit_offset = bit_position;
                        info->contact_count_bits = report_size;
                    }
                }

                if (in_pointer) {
                    if (pointer_report_id == -1) {
                        pointer_report_id = current_report_id;
                    }
                    if (usage_page == 0x09 && !got_pointer_button) {
                        info->pointer_button_bit_offset = bit_position;
                        got_pointer_button = true;
                    } else if (usage_page == 0x01 && !(data & 0x04)) { // Absolute
                        for (uint8_t f = 0; f < report_count; f++) {
                            uint16_t u = (f < ptr_uq_n) ? ptr_uq[f]
                                : (ptr_uq_n ? ptr_uq[ptr_uq_n - 1] : last_usage);
                            uint16_t off = bit_position + (uint16_t)f * report_size;
                            if (u == 0x30) {
                                info->pointer_x_bit_offset = off;
                                info->pointer_x_bits = report_size;
                                info->pointer_x_logical_max = logical_max;
                            } else if (u == 0x31) {
                                info->pointer_y_bit_offset = off;
                                info->pointer_y_bits = report_size;
                                info->pointer_y_logical_max = logical_max;
                            }
                        }
                    }
                }

                bit_position += field_bits;
                last_usage = 0;
            } else if (bTag == 9) { // Output
                last_usage = 0;
            } else if (bTag == 11) { // Feature
                if (in_device_config && !info->has_input_mode) {
                    info->has_input_mode = true;
                    info->input_mode_report_id = (uint8_t)current_report_id;
                }
                if (usage_page == 0x0D && last_usage == 0x55 && !info->contact_count_max_report_id) {
                    info->contact_count_max_report_id = (uint8_t)current_report_id;
                }
                if (last_usage == 0xC5 && !info->cert_blob_report_id) {
                    info->cert_blob_report_id = (uint8_t)current_report_id;
                }
                last_usage = 0;
            }
            ptr_uq_n = 0;
        }
    }

    if (finger_count > 0) {
        uint16_t after_contacts = info->first_contact_bit_offset + finger_count * info->contact_stride_bits;
        uint16_t after_cc = info->contact_count_bit_offset + info->contact_count_bits;
        uint16_t end_bits = (after_contacts > after_cc) ? after_contacts : after_cc;
        info->report_length_bytes = (end_bits + 7) / 8;
        if (info->uses_report_id) {
            info->report_length_bytes += 1;
        }
    }
    info->max_contacts = finger_count;

    if (pointer_report_id >= 0 && pointer_report_id != finger_report_id &&
        got_pointer_button && info->pointer_x_bits > 0 && info->pointer_y_bits > 0 &&
        info->pointer_x_logical_max > 0 && info->pointer_y_logical_max > 0) {
        info->has_pointer_fallback = true;
        info->pointer_report_id = (uint8_t)pointer_report_id;
        uint16_t bx = info->pointer_button_bit_offset + 1;
        uint16_t ex = info->pointer_x_bit_offset + info->pointer_x_bits;
        uint16_t ey = info->pointer_y_bit_offset + info->pointer_y_bits;
        uint16_t end_bits = bx;
        if (ex > end_bits) {
            end_bits = ex;
        }
        if (ey > end_bits) {
            end_bits = ey;
        }
        info->pointer_report_length_bytes = (end_bits + 7) / 8;
        if (info->uses_report_id) {
            info->pointer_report_length_bytes += 1;
        }
    }

    info->valid = (saw_touchscreen_root && finger_count > 0 && info->x_bits > 0 && info->y_bits > 0);
    return info->valid;
}

// ============================================================================
// Report decode + publish (from MMBasic process_touch_report / touch_publish)
// ============================================================================
static int16_t touch_scale_axis(uint32_t raw, int32_t logical_max, int res) {
    if (logical_max > 0 && res > 0) {
        if (raw > (uint32_t)logical_max) {
            raw = (uint32_t)logical_max;
        }
        int32_t v = (int32_t)((raw * (uint32_t)res) / (uint32_t)logical_max);
        if (v >= res) {
            v = res - 1;
        }
        return (int16_t)v;
    }
    return (int16_t)raw;
}

static void touch_publish(touch_report_t *out) {
    usb_touch_last_us = time_us_64();
    if (out->count > 0 && out->contacts[0].tip) {
        usb_touch_x = out->contacts[0].x;
        usb_touch_y = out->contacts[0].y;
        usb_touch_active = true;
    } else {
        usb_touch_active = false;
    }
    if (out->count >= 2 && out->contacts[1].tip) {
        usb_touch_x2 = out->contacts[1].x;
        usb_touch_y2 = out->contacts[1].y;
        usb_touch_active2 = true;
    } else {
        usb_touch_active2 = false;
    }
    uint8_t nc = out->count;
    if (nc > MAX_TOUCH_CONTACTS) {
        nc = MAX_TOUCH_CONTACTS;
    }
    for (uint8_t k = 0; k < nc; k++) {
        usb_touch_xn[k] = out->contacts[k].x;
        usb_touch_yn[k] = out->contacts[k].y;
    }
    usb_touch_count = nc;

    // Single-finger down/up edges drive the swipe / tap / long-press machine.
    static bool was_active = false;
    if (usb_touch_active && !was_active) {
        touch_gesture_on_down(usb_touch_x, usb_touch_y);
    } else if (!usb_touch_active && was_active) {
        touch_gesture_on_up(usb_touch_x, usb_touch_y);
    }
    was_active = usb_touch_active;

    // Two-finger pinch/rotate/two-tap: both contacts down -> start, either up -> end.
    static bool pinch_was_both = false;
    bool now_both = usb_touch_active && usb_touch_active2;
    if (now_both && !pinch_was_both) {
        touch_gesture_pinch_start(usb_touch_x, usb_touch_y, usb_touch_x2, usb_touch_y2);
    } else if (!now_both && pinch_was_both) {
        touch_gesture_pinch_end(usb_touch_x, usb_touch_y, usb_touch_x2, usb_touch_y2);
    }
    pinch_was_both = now_both;

    // Long-press fires mid-hold (single finger only).
    touch_gesture_tick(usb_touch_x, usb_touch_y, usb_touch_active && !usb_touch_active2);
}

static bool process_pointer_report(const uint8_t *bytes, uint16_t payload_len,
    const touch_info_t *info, touch_report_t *out) {
    uint32_t need = (uint32_t)info->pointer_button_bit_offset + 1;
    uint32_t nx = (uint32_t)info->pointer_x_bit_offset + info->pointer_x_bits;
    uint32_t ny = (uint32_t)info->pointer_y_bit_offset + info->pointer_y_bits;
    if (nx > need) {
        need = nx;
    }
    if (ny > need) {
        need = ny;
    }
    if ((uint32_t)payload_len * 8u < need) {
        out->count = 0;
        return false;
    }
    bool tip = touch_read_bits(bytes, info->pointer_button_bit_offset, 1) != 0;
    touch_contact_t *tc = &out->contacts[0];
    tc->tip = tip;
    tc->in_range = tip;
    tc->id = 0;
    uint32_t raw_x = touch_read_bits(bytes, info->pointer_x_bit_offset, info->pointer_x_bits);
    uint32_t raw_y = touch_read_bits(bytes, info->pointer_y_bit_offset, info->pointer_y_bits);
    tc->x = touch_scale_axis(raw_x, info->pointer_x_logical_max, HRes);
    tc->y = touch_scale_axis(raw_y, info->pointer_y_logical_max, VRes);
    out->count = tip ? 1 : 0;
    touch_publish(out);
    return true;
}

static bool process_touch_report(const uint8_t *report, uint16_t len,
    const touch_info_t *info, touch_report_t *out) {
    if (!report || !info || !out || !info->valid) {
        return false;
    }
    const uint8_t *bytes = report;
    uint16_t payload_len = len;

    if (info->uses_report_id) {
        if (payload_len < 1) {
            out->count = 0;
            return false;
        }
        if (info->has_pointer_fallback && bytes[0] == info->pointer_report_id) {
            return process_pointer_report(bytes + 1, payload_len - 1, info, out);
        }
        if (bytes[0] != info->report_id) {
            out->count = 0;
            return false;
        }
        bytes++;
        payload_len--;
    }

    if ((uint32_t)payload_len * 8u <
        (uint32_t)info->contact_count_bit_offset + info->contact_count_bits) {
        out->count = 0;
        return false;
    }

    uint8_t scan = touch_read_bits(bytes, info->contact_count_bit_offset, info->contact_count_bits);
    if (scan > info->max_contacts) {
        scan = info->max_contacts;
    }
    if (scan > MAX_TOUCH_CONTACTS) {
        scan = MAX_TOUCH_CONTACTS;
    }

    uint8_t live = 0;
    for (uint8_t c = 0; c < scan; c++) {
        uint16_t base = info->first_contact_bit_offset + (uint16_t)c * info->contact_stride_bits;
        if (touch_read_bits(bytes, base + info->tip_switch_bit_offset, 1) == 0) {
            continue;
        }
        touch_contact_t *tc = &out->contacts[live++];
        tc->tip = true;
        tc->in_range = touch_read_bits(bytes, base + info->in_range_bit_offset, 1) != 0;
        tc->id = (uint8_t)touch_read_bits(bytes, base + info->contact_id_bit_offset, info->contact_id_bits);
        uint32_t raw_x = touch_read_bits(bytes, base + info->x_bit_offset, info->x_bits);
        uint32_t raw_y = touch_read_bits(bytes, base + info->y_bit_offset, info->y_bits);
        tc->x = touch_scale_axis(raw_x, info->x_logical_max, HRes);
        tc->y = touch_scale_axis(raw_y, info->y_logical_max, VRes);
    }
    out->count = live;
    touch_publish(out);
    return true;
}

// --- Report reassembly (from MMBasic touch_reassemble) ---------------------
static uint16_t touch_expected_len(const touch_info_t *info, uint8_t rid) {
    if (!info->uses_report_id) {
        return info->report_length_bytes;
    }
    if (rid == info->report_id) {
        return info->report_length_bytes;
    }
    if (info->has_pointer_fallback && rid == info->pointer_report_id) {
        return info->pointer_report_length_bytes;
    }
    return 0;
}

static void touch_reassemble(const uint8_t *report, uint16_t len,
    const touch_info_t *info, touch_report_t *out) {
    static uint8_t buf[256];
    static uint16_t fill = 0;

    if ((uint32_t)fill + len > sizeof(buf)) {
        fill = 0;
    }
    uint16_t copy = (len > sizeof(buf)) ? (uint16_t)sizeof(buf) : len;
    memcpy(buf + fill, report, copy);
    fill += copy;

    uint16_t pos = 0;
    while (pos < fill) {
        uint16_t exp = touch_expected_len(info, buf[pos]);
        if (exp == 0) {
            pos++;
            continue;
        }
        if (fill - pos < exp) {
            break;
        }
        process_touch_report(buf + pos, exp, info, out);
        pos += exp;
    }
    if (pos > 0) {
        if (pos < fill) {
            memmove(buf, buf + pos, fill - pos);
        }
        fill -= pos;
    }
}

// ============================================================================
// Public API — called from mp_usbh.c HID callbacks
// ============================================================================
bool usb_touch_probe_mount(uint8_t dev_addr, uint8_t instance,
    const uint8_t *desc_report, uint16_t desc_len) {
    touch_info_t tinfo;
    if (!analyze_touch_descriptor(desc_report, desc_len, &tinfo)) {
        return false; // not a multi-touch digitizer
    }
    touch_addr = dev_addr;
    touch_inst = instance;
    memcpy(&touch_info, &tinfo, sizeof(touch_info));
    usb_touch_present = true;
    usb_touch_active = false;
    usb_touch_active2 = false;
    usb_touch_count = 0;
    // Switch out of boot protocol so we get the multi-touch report layout.
    tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
    // Arm the Windows touch bring-up handshake for dual-mode panels: Input Mode
    // write (step 10) if present, else the GET_FEATURE cert/contact-count reads.
    touch_init_step = tinfo.has_input_mode ? 10 : (tinfo.has_pointer_fallback ? 1 : 0);
    return true;
}

bool usb_touch_owns(uint8_t dev_addr, uint8_t instance) {
    return usb_touch_present && dev_addr == touch_addr && instance == touch_inst;
}

void usb_touch_set_slot(int slot) {
    touch_slot_num = slot;
}

void usb_touch_on_report(uint8_t dev_addr, uint8_t instance,
    const uint8_t *report, uint16_t len) {
    if (!usb_touch_owns(dev_addr, instance)) {
        return;
    }
    static touch_report_t out;
    touch_reassemble(report, len, &touch_info, &out);
}

void usb_touch_on_umount(uint8_t dev_addr, uint8_t instance) {
    if (!usb_touch_owns(dev_addr, instance)) {
        return;
    }
    usb_touch_present = false;
    usb_touch_active = false;
    usb_touch_active2 = false;
    usb_touch_count = 0;
    touch_addr = touch_inst = 0xFF;
    touch_init_step = 0;
    touch_slot_num = 0;
}

// Digitizer-init handshake + no-report watchdog. Pumped from mp_usbh_task.
void usb_touch_task(void) {
    if (!usb_touch_present) {
        return;
    }
    // No-report watchdog: some panels omit the release report, leaving the
    // touch stuck "down". Force a lift (and close the gesture) after 100 ms.
    if (usb_touch_active && (time_us_64() - usb_touch_last_us) > 100000ULL) {
        touch_gesture_on_up(usb_touch_x, usb_touch_y);
        usb_touch_active = false;
        usb_touch_active2 = false;
        usb_touch_count = 0;
    }

    // Handshake steps (see MMBasic hid_app_task). In-flight steps (2/4/11/12)
    // are cleared by the completion callbacks below.
    if (touch_init_step == 10) {
        if (tuh_hid_get_report(touch_addr, touch_inst, touch_info.input_mode_report_id,
            HID_REPORT_TYPE_FEATURE, touch_feature_buf, 2)) {
            touch_init_step = 12;
        }
    } else if (touch_init_step == 13) {
        if (tuh_hid_set_report(touch_addr, touch_inst, touch_info.input_mode_report_id,
            HID_REPORT_TYPE_FEATURE, touch_feature_buf, 2)) {
            touch_init_step = 11;
        }
    } else if (touch_init_step == 1 || touch_init_step == 3) {
        uint8_t rid = (touch_init_step == 1) ? touch_info.cert_blob_report_id
            : touch_info.contact_count_max_report_id;
        if (rid == 0) {
            touch_init_step = (touch_init_step == 1) ? 3 : 0;
        } else if (tuh_hid_get_report(touch_addr, touch_inst, rid,
            HID_REPORT_TYPE_FEATURE, touch_feature_buf, sizeof(touch_feature_buf))) {
            touch_init_step++;
        }
    }
}

void usb_touch_get_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id) {
    (void)report_id;
    if (!usb_touch_owns(dev_addr, instance)) {
        return;
    }
    if (touch_init_step == 2) {
        touch_init_step = 3;
    } else if (touch_init_step == 4) {
        touch_init_step = 0;
    } else if (touch_init_step == 12) {
        // Read-modify-write Input Mode: force Device Mode = 0x02 (touchscreen),
        // keep the returned identifier, then write it back (step 13).
        touch_feature_buf[0] = 0x02;
        touch_init_step = 13;
    }
}

void usb_touch_set_complete(uint8_t dev_addr, uint8_t instance) {
    if (!usb_touch_owns(dev_addr, instance)) {
        return;
    }
    if (touch_init_step == 11) {
        touch_init_step = 0; // Input Mode write done
    }
}

// ============================================================================
// Query — read state / gestures for the `touch` module (no USB access)
// ============================================================================
int usb_touch_query(int code, int arg) {
    switch (code) {
        case TQ_X: return usb_touch_active ? usb_touch_x : TOUCH_ERROR;
        case TQ_Y: return usb_touch_active ? usb_touch_y : TOUCH_ERROR;
        case TQ_DOWN: return usb_touch_active ? 1 : 0;
        case TQ_UP: return usb_touch_active ? 0 : 1;
        case TQ_X2: return usb_touch_active2 ? usb_touch_x2 : TOUCH_ERROR;
        case TQ_Y2: return usb_touch_active2 ? usb_touch_y2 : TOUCH_ERROR;
        case TQ_XN:
            if (arg == 0) {
                return usb_touch_count;
            }
            return (arg >= 1 && arg <= usb_touch_count) ? usb_touch_xn[arg - 1] : TOUCH_ERROR;
        case TQ_YN:
            if (arg == 0) {
                return usb_touch_count;
            }
            return (arg >= 1 && arg <= usb_touch_count) ? usb_touch_yn[arg - 1] : TOUCH_ERROR;
        case TQ_SWL:
            if (touch_swipe_dir == 1) { touch_swipe_dir = 0; return 1; }
            return 0;
        case TQ_SWR:
            if (touch_swipe_dir == 2) { touch_swipe_dir = 0; return 1; }
            return 0;
        case TQ_SWU:
            if (touch_swipe_dir == 3) { touch_swipe_dir = 0; return 1; }
            return 0;
        case TQ_SWD:
            if (touch_swipe_dir == 4) { touch_swipe_dir = 0; return 1; }
            return 0;
        case TQ_SWIPE: { int v = touch_swipe_dir; touch_swipe_dir = 0; return v; }
        case TQ_EXPAND:
            if (touch_pinch_dir == 1) { touch_pinch_dir = 0; return 1; }
            return 0;
        case TQ_CONTRACT:
            if (touch_pinch_dir == 2) { touch_pinch_dir = 0; return 1; }
            return 0;
        case TQ_PINCH: { int v = touch_pinch_dir; touch_pinch_dir = 0; return v; }
        case TQ_TAP: { int v = touch_tap; touch_tap = 0; return v; }
        case TQ_HOLD: { int v = touch_longpress; touch_longpress = 0; return v; }
        case TQ_DTAP: { int v = touch_doubletap; touch_doubletap = 0; return v; }
        case TQ_CW:
            if (touch_rotate_dir == 1) { touch_rotate_dir = 0; return 1; }
            return 0;
        case TQ_CCW:
            if (touch_rotate_dir == 2) { touch_rotate_dir = 0; return 1; }
            return 0;
        case TQ_ROTATE: { int v = touch_rotate_dir; touch_rotate_dir = 0; return v; }
        case TQ_TTAP: { int v = touch_twotap; touch_twotap = 0; return v; }
        case TQ_PRESENT: return usb_touch_present ? 1 : 0;
        case TQ_SLOT: return touch_slot_num;
        default: return TOUCH_ERROR;
    }
}

#endif // MICROPY_HW_USB_HOST
