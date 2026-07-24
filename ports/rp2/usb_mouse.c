/*
 * USB mouse support for the Pico Computer 3.
 *
 * Originally ported from MMBasic (PicoMite) USBKeyboard.c, which recognised
 * three fixed report layouts (8/12/16-bit X/Y). Generalised since: the report
 * descriptor is walked once at mount and the exact bit offset / width /
 * signedness of each field (buttons, X, Y, wheel, AC pan) is recorded, along
 * with the report ID that carries them; input reports are then decoded by bit
 * extraction, so any single-pointer mouse layout works — arbitrary padding,
 * field order, button counts and X/Y widths, and multi-report-ID descriptors
 * (movement decoded from the pointer report, other IDs ignored).
 *
 * The mouse is switched to report protocol at mount (as MMBasic does). If the
 * device stays in boot protocol (SET_PROTOCOL refused) or the descriptor parse
 * finds no X/Y, the decoder falls back to the fixed boot layout
 * (buttons, X8, Y8[, wheel]).
 *
 * Cursor accumulation (process_mouse_input in MMBasic's KeyboardMap.c) is
 * unchanged: deltas accumulate into a virtual cursor clamped to the screen,
 * with button, wheel and double-click tracking.
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

// --- Report layout captured from the HID report descriptor ------------------
typedef struct {
    bool present;
    uint16_t bit_offset; // within the report, excluding any report-ID byte
    uint8_t bit_size;
    bool is_signed;      // Logical Minimum < 0 on the declaring Input item
    uint8_t report_id;   // report this field belongs to (0 = no IDs used)
} mouse_field_t;

typedef struct {
    bool valid;            // X and Y found: bit-extraction decode possible
    mouse_field_t buttons; // run of 1-bit fields folded into one bitmap
    mouse_field_t x, y, wheel, pan;
    bool uses_report_id;
    uint8_t report_id;     // ID of the report carrying X/Y (0 if none used)
    uint8_t report_length; // declared Input bits of that report, in bytes
    uint8_t min_len;       // minimum report bytes needed to decode all fields
} mouse_layout_t;

// --- Published mouse state (read by usb_mouse_query) -------------------------
static uint8_t mouse_addr = 0xFF, mouse_inst = 0xFF;
static bool mouse_present = false;
static int mouse_slot_num = 0; // 1-based HID slot
static mouse_layout_t mouse_layout;
static float mouse_speed = 1.0f; // Option.mousespeed: raw delta divided by this

static volatile int mouse_ax = 0, mouse_ay = 0; // accumulated position (screen coords)
static volatile int mouse_az = 0;               // wheel accumulator
static volatile int mouse_l = 0, mouse_r = 0, mouse_c = 0; // buttons
static volatile int mouse_buttons = 0;          // raw button bitmap (bits L/R/M)
static volatile int mouse_dclick = 0;           // double-click (clear on read)

// --- Descriptor parser -------------------------------------------------------
// Records the first field seen for each usage of interest. Understands the
// items a pointer descriptor uses: Usage Page / Usage (incl. 4-byte extended
// usages), Usage Min/Max ranges, Logical Minimum (signedness), Report Size /
// Count / ID, and Input items including constant padding. Not handled: Push/
// Pop (unused by mice) — long items are skipped.

#define MOUSE_MAX_USAGES 8
#define MOUSE_MAX_REPORT_IDS 8

// Per-report-ID Input bit counts: each Report ID opens its own bit space.
typedef struct {
    uint8_t id;
    uint16_t bits;
} mouse_rbits_t;

static uint16_t rbits_get(const mouse_rbits_t *tab, uint8_t n, uint8_t id) {
    for (uint8_t k = 0; k < n; k++) {
        if (tab[k].id == id) {
            return tab[k].bits;
        }
    }
    return 0;
}

static void rbits_set(mouse_rbits_t *tab, uint8_t *n, uint8_t id, uint16_t bits) {
    for (uint8_t k = 0; k < *n; k++) {
        if (tab[k].id == id) {
            tab[k].bits = bits;
            return;
        }
    }
    if (*n < MOUSE_MAX_REPORT_IDS) {
        tab[*n].id = id;
        tab[*n].bits = bits;
        (*n)++;
    }
}

static void capture_field(mouse_field_t *f, uint16_t off, uint8_t bits,
    bool is_signed, uint8_t report_id) {
    if (!f->present) {
        f->present = true;
        f->bit_offset = off;
        f->bit_size = bits;
        f->is_signed = is_signed;
        f->report_id = report_id;
    }
}

static bool analyze_mouse_descriptor(const uint8_t *desc, uint16_t desc_len,
    mouse_layout_t *out) {
    memset(out, 0, sizeof(*out));
    if (!desc || desc_len == 0) {
        return false;
    }

    // Global item state.
    uint16_t usage_page = 0;
    int32_t logical_min = 0;
    uint8_t report_size = 0, report_count = 0;
    uint8_t cur_id = 0;
    bool saw_report_id = false;
    // Local item state (cleared after every Main item).
    uint32_t usages[MOUSE_MAX_USAGES]; // page << 16 | usage
    uint8_t n_usages = 0;
    uint32_t usage_min = 0;
    bool have_usage_range = false;
    // Input bit position within the current report ID's report.
    uint16_t bit_pos = 0;
    mouse_rbits_t rbits[MOUSE_MAX_REPORT_IDS];
    uint8_t n_rbits = 0;

    for (uint16_t i = 0; i < desc_len;) {
        uint8_t prefix = desc[i++];
        if (prefix == 0xFE) { // long item: bDataSize follows, then tag + data
            if (i < desc_len) {
                i += 2 + desc[i];
            }
            continue;
        }
        uint8_t bSize = prefix & 0x03;
        if (bSize == 3) {
            bSize = 4; // HID spec: size code 3 means 4 bytes
        }
        uint8_t bType = (prefix >> 2) & 0x03;
        uint8_t bTag = (prefix >> 4) & 0x0F;
        uint32_t data = 0;
        for (uint8_t j = 0; j < bSize && i + j < desc_len; j++) {
            data |= (uint32_t)desc[i + j] << (j * 8);
        }
        int32_t sdata = (int32_t)data;
        if (bSize == 1) {
            sdata = (int8_t)data;
        } else if (bSize == 2) {
            sdata = (int16_t)data;
        }
        i += bSize;

        if (bType == 1) { // Global
            switch (bTag) {
                case 0:
                    usage_page = data;
                    break;
                case 1:
                    logical_min = sdata;
                    break;
                case 7:
                    report_size = data;
                    break;
                case 8: // Report ID: switch to that report's own bit space
                    rbits_set(rbits, &n_rbits, cur_id, bit_pos);
                    saw_report_id = true;
                    cur_id = data;
                    bit_pos = rbits_get(rbits, n_rbits, cur_id);
                    break;
                case 9:
                    report_count = data;
                    break;
                default:
                    break;
            }
        } else if (bType == 2) { // Local
            switch (bTag) {
                case 0: // Usage; 4-byte form carries the page in the top half
                    if (n_usages < MOUSE_MAX_USAGES) {
                        usages[n_usages++] = (bSize == 4)
                            ? data : (((uint32_t)usage_page << 16) | data);
                    }
                    break;
                case 1:
                    usage_min = data;
                    have_usage_range = true;
                    break;
                default:
                    break;
            }
        } else if (bType == 0) { // Main
            if (bTag == 8) { // Input
                bool constant = data & 0x01;
                if (!constant && report_size && report_count) {
                    for (uint16_t f = 0; f < report_count; f++) {
                        // Usage for field f: queued usages distribute in order
                        // (last repeats, per spec); a Usage Min/Max range
                        // enumerates from its minimum.
                        uint32_t u = 0;
                        if (n_usages) {
                            u = usages[f < n_usages ? f : n_usages - 1];
                        } else if (have_usage_range) {
                            u = ((uint32_t)usage_page << 16) | (usage_min + f);
                        }
                        uint16_t page = u >> 16;
                        uint16_t usage = u & 0xFFFF;
                        uint16_t off = bit_pos + f * report_size;
                        bool sgn = logical_min < 0;
                        if (page == 0x01) { // Generic Desktop
                            if (usage == 0x30) {
                                capture_field(&out->x, off, report_size, sgn, cur_id);
                            } else if (usage == 0x31) {
                                capture_field(&out->y, off, report_size, sgn, cur_id);
                            } else if (usage == 0x38) {
                                capture_field(&out->wheel, off, report_size, sgn, cur_id);
                            }
                        } else if (page == 0x09) { // Buttons
                            // Fold the run of 1-bit button fields into one
                            // little-endian bitmap, first button = bit 0.
                            if (!out->buttons.present && report_size == 1) {
                                uint16_t nb = report_count - f;
                                capture_field(&out->buttons, off,
                                    nb > 8 ? 8 : (uint8_t)nb, false, cur_id);
                            }
                        } else if (page == 0x0C && usage == 0x0238) { // AC Pan
                            capture_field(&out->pan, off, report_size, sgn, cur_id);
                        }
                    }
                }
                bit_pos += (uint16_t)report_size * report_count;
            }
            // Local items only apply to the next Main item.
            n_usages = 0;
            usage_min = 0;
            have_usage_range = false;
        }
    }
    rbits_set(rbits, &n_rbits, cur_id, bit_pos);

    // X and Y must live in the same report for a usable pointer layout.
    if (!out->x.present || !out->y.present || out->y.report_id != out->x.report_id) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    out->valid = true;
    out->uses_report_id = saw_report_id;
    out->report_id = out->x.report_id;
    // Fields declared under a different report ID arrive in different reports.
    if (out->wheel.present && out->wheel.report_id != out->report_id) {
        out->wheel.present = false;
    }
    if (out->pan.present && out->pan.report_id != out->report_id) {
        out->pan.present = false;
    }
    if (out->buttons.present && out->buttons.report_id != out->report_id) {
        out->buttons.present = false;
    }

    uint16_t bits = rbits_get(rbits, n_rbits, out->report_id);
    out->report_length = (bits + 7) / 8;
    uint16_t need = 0;
    const mouse_field_t *fields[] = { &out->buttons, &out->x, &out->y, &out->wheel, &out->pan };
    for (unsigned k = 0; k < sizeof(fields) / sizeof(fields[0]); k++) {
        if (fields[k]->present) {
            uint16_t end = fields[k]->bit_offset + fields[k]->bit_size;
            if (end > need) {
                need = end;
            }
        }
    }
    out->min_len = (need + 7) / 8;
    return true;
}

// --- Report field extraction -------------------------------------------------
// HID reports are little-endian bit streams: bit n lives in byte n/8, bit n%8.
static uint32_t report_bits(const uint8_t *p, uint16_t off, uint8_t n) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint16_t b = off + i;
        if (p[b >> 3] & (1u << (b & 7))) {
            v |= 1u << i;
        }
    }
    return v;
}

static int report_field(const uint8_t *p, const mouse_field_t *f) {
    if (!f->present) {
        return 0;
    }
    uint32_t v = report_bits(p, f->bit_offset, f->bit_size);
    if (f->is_signed && f->bit_size < 32 && (v & (1u << (f->bit_size - 1)))) {
        v |= ~((1u << f->bit_size) - 1);
    }
    return (int)(int32_t)v;
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
    analyze_mouse_descriptor(desc_report, desc_len, &mouse_layout);
    // Switch from boot protocol to report protocol so the reports match the
    // descriptor layout decoded above. TinyUSB puts boot-capable mice into
    // boot protocol during enumeration; MMBasic issues exactly this call from
    // its mount callback (USBKeyboard.c mouse path) and it's the only
    // mount-time control transfer allowed here.
    tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
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
    if (!usb_mouse_owns(dev_addr, instance) || !report || len == 0) {
        return;
    }
    float sp = (mouse_speed == 0.0f) ? 1.0f : mouse_speed;
    int x_delta = 0, y_delta = 0, wheel = 0;
    uint8_t buttons = 0;

    // A device the SET_PROTOCOL didn't stick on still sends boot reports; a
    // descriptor with no X/Y leaves us nothing better than the boot layout.
    bool boot = tuh_hid_get_protocol(dev_addr, instance) == HID_PROTOCOL_BOOT;
    if (boot || !mouse_layout.valid) {
        if (len < 3) {
            return;
        }
        buttons = report[0];
        x_delta = (int)((int8_t)report[1] / sp);
        y_delta = (int)((int8_t)report[2] / sp);
        if (len > 3) {
            wheel = (int8_t)report[3];
        }
    } else {
        if (mouse_layout.uses_report_id) {
            if (report[0] != mouse_layout.report_id) {
                return; // another of the interface's reports (e.g. consumer keys)
            }
            report++;
            len--;
        }
        if (len < mouse_layout.min_len) {
            return; // truncated report
        }
        buttons = (uint8_t)report_field(report, &mouse_layout.buttons);
        x_delta = (int)(report_field(report, &mouse_layout.x) / sp);
        y_delta = (int)(report_field(report, &mouse_layout.y) / sp);
        wheel = report_field(report, &mouse_layout.wheel);
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
        case MQ_T: return mouse_layout.wheel.present ? 3 : 0;
        case MQ_PRESENT: return mouse_present ? 1 : 0;
        case MQ_SLOT: return mouse_slot_num;
        default: return -1;
    }
}

#endif // MICROPY_HW_USB_HOST
