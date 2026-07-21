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

// USB HID gamepad decoding for the Pico Computer 3. The report handlers, the
// known-controller table, checkpush / process_generic_gamepad and the
// process_xbox / process_sony_ds3 / process_sony_ds4 decoders are ported
// VERBATIM from MMBasic (PicoMite USBKeyboard.c) -- the proven mappings and the
// documented 16-bit button bitmap. mp_usbh.c routes protocol-NONE HID devices
// here; the `gamepad` Python module (usb_gamepad_mod.c) reads the state, exactly
// as MMBasic's DEVICE(GAMEPAD n, "...") does.

#include "py/runtime.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "tusb.h"
#include "usb_gamepad.h"

// --- per-channel state (MMBasic nunstruct fields used by the gamepad path) ---
#define GP_NCHAN 5 // index 1..4 used
typedef struct {
    bool active;
    uint8_t addr, inst;
    uint16_t vid, pid;
    int ax, ay;         // left stick   (MMBasic nunstruct.ax / .ay)
    int Z, C;           // right stick  (MMBasic nunstruct.Z / .C)
    int L, R;           // analog triggers
    uint16_t x0;        // button bitmap (current)
    uint16_t x1;        // change-detect mask (default 0xFFFF)
    uint16_t y0;        // hat direction
    int16_t imu[6];     // gyro[3] + accel[3] (PS4)
    uint64_t type;      // GP_TYPE_*
    volatile bool changed;
    uint8_t report[65]; // [0] = length, [1..] = raw report bytes
    uint8_t report_len;
} gp_state_t;

static gp_state_t gp[GP_NCHAN];

// --- MMBasic button-to-report mapping model (verbatim) ----------------------
struct s_Buttons {
    uint8_t index; // report byte; 0xFF = unused
    uint8_t code;  // bit 0..7 (pressed if set), 128..135 (pressed if clear),
                   // 64 (value < 64), 192 (value > 192)
};
struct s_Gamepad {
    uint16_t vid, pid;
    struct s_Buttons b_R, b_START, b_HOME, b_SELECT, b_L, b_DOWN, b_RIGHT, b_UP,
        b_LEFT, b_R2, b_X, b_A, b_Y, b_B, b_L2, b_TOUCH;
};

// Known controllers -- verbatim MMBasic Gamepads[] table.
static const struct s_Gamepad Gamepads[] = {
    {.vid = 0x0810, .pid = 0xE501, .b_R = {6, 1}, .b_START = {6, 5}, .b_HOME = {0xFF, 0}, .b_SELECT = {6, 4}, .b_L = {6, 0}, .b_DOWN = {4, 192}, .b_RIGHT = {3, 192}, .b_UP = {4, 64}, .b_LEFT = {3, 64}, .b_R2 = {0xFF, 0}, .b_X = {5, 4}, .b_A = {5, 5}, .b_Y = {5, 7}, .b_B = {5, 6}, .b_L2 = {0xFF, 0}, .b_TOUCH = {0xFF, 0}},
    {.vid = 0x79, .pid = 0x11, .b_R = {6, 1}, .b_START = {6, 5}, .b_HOME = {255, 0}, .b_SELECT = {6, 4}, .b_L = {6, 0}, .b_DOWN = {4, 192}, .b_RIGHT = {3, 192}, .b_UP = {4, 64}, .b_LEFT = {3, 64}, .b_R2 = {255, 0}, .b_X = {5, 4}, .b_A = {5, 5}, .b_Y = {5, 7}, .b_B = {5, 6}, .b_L2 = {255, 0}, .b_TOUCH = {255, 0}},
    {.vid = 0x081F, .pid = 0xE401, .b_R = {6, 1}, .b_START = {6, 5}, .b_HOME = {0xFF, 0}, .b_SELECT = {6, 4}, .b_L = {6, 0}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {0xFF, 0}, .b_X = {5, 4}, .b_A = {5, 5}, .b_Y = {5, 7}, .b_B = {5, 6}, .b_L2 = {0xFF, 0}, .b_TOUCH = {0xFF, 0}},
    {.vid = 0x1C59, .pid = 0x26, .b_R = {6, 1}, .b_START = {6, 3}, .b_HOME = {255, 0}, .b_SELECT = {6, 2}, .b_L = {6, 0}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {255, 0}, .b_X = {5, 7}, .b_A = {5, 6}, .b_Y = {5, 4}, .b_B = {5, 5}, .b_L2 = {255, 0}, .b_TOUCH = {255, 0}},
    {.vid = 0x6A3, .pid = 0x107, .b_R = {3, 7}, .b_START = {3, 5}, .b_HOME = {255, 0}, .b_SELECT = {3, 4}, .b_L = {3, 6}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {3, 7}, .b_X = {3, 1}, .b_A = {3, 3}, .b_Y = {3, 0}, .b_B = {3, 2}, .b_L2 = {255, 0}, .b_TOUCH = {255, 0}},
    {.vid = 0x11FF, .pid = 0x3331, .b_R = {6, 3}, .b_START = {6, 1}, .b_HOME = {6, 6}, .b_SELECT = {6, 0}, .b_L = {6, 2}, .b_DOWN = {1, 192}, .b_RIGHT = {0, 192}, .b_UP = {1, 64}, .b_LEFT = {0, 64}, .b_R2 = {6, 5}, .b_X = {5, 4}, .b_A = {5, 6}, .b_Y = {5, 5}, .b_B = {5, 7}, .b_L2 = {6, 4}, .b_TOUCH = {6, 7}},
    {.vid = 0x0583, .pid = 0x2060, .b_R = {0x02, 0x05}, .b_START = {0x02, 0x07}, .b_HOME = {0xFF, 0x00}, .b_SELECT = {0x02, 0x06}, .b_L = {0x02, 0x04}, .b_DOWN = {0x01, 0xC0}, .b_RIGHT = {0x00, 0xC0}, .b_UP = {0x01, 0x40}, .b_LEFT = {0x00, 0x40}, .b_R2 = {0x03, 0x05}, .b_X = {0x02, 0x02}, .b_A = {0x02, 0x00}, .b_Y = {0x02, 0x03}, .b_B = {0x02, 0x01}, .b_L2 = {0x03, 0x04}, .b_TOUCH = {0xFF, 0x00}},
    {0},
};

static struct s_Gamepad MyGamepad = {0};

// Button bit positions in the 16-bit bitmap (verbatim MMBasic p_* values).
#define p_R GP_R
#define p_START GP_START
#define p_HOME GP_HOME
#define p_SELECT GP_SELECT
#define p_L GP_L
#define p_DOWN GP_DOWN
#define p_RIGHT GP_RIGHT
#define p_UP GP_UP
#define p_LEFT GP_LEFT
#define p_R2 GP_R2
#define p_X GP_X
#define p_A GP_A
#define p_Y GP_Y
#define p_B GP_B
#define p_L2 GP_L2
#define p_TOUCH GP_TOUCH

// --- DualShock 4 report layout (verbatim MMBasic) ---------------------------
typedef struct TU_ATTR_PACKED {
    uint8_t x, y, z, rz; // joystick
    struct {
        uint8_t dpad : 4; // hat; 8 = released, 0=N,1=NE,2=E,3=SE,4=S,5=SW,6=W,7=NW
        uint8_t square : 1;
        uint8_t cross : 1;
        uint8_t circle : 1;
        uint8_t triangle : 1;
    };
    struct {
        uint8_t l1 : 1, r1 : 1, l2 : 1, r2 : 1, share : 1, option : 1, l3 : 1, r3 : 1;
    };
    struct {
        uint8_t ps : 1, tpad : 1, counter : 6;
    };
    uint8_t l2_trigger, r2_trigger;
    uint16_t timestamp;
    uint8_t battery;
    int16_t gyro[3];
    int16_t accel[3];
} sony_ds4_report_t;

// --- VID/PID device detectors (verbatim MMBasic lists) ----------------------
static inline bool is_xbox(uint16_t vid, uint16_t pid) {
    return ((vid == 0x11c0 && pid == 0x5500)
        || (vid == 0x11c1 && pid == 0x9101)
        || (vid == 0x2F24 && pid == 0x0048));
}
static inline bool is_sony_ds3(uint16_t vid, uint16_t pid) {
    return (vid == 0x054c && pid == 0x0268);
}
static inline bool is_sony_ds4(uint16_t vid, uint16_t pid) {
    return ((vid == 0x054c && (pid == 0x09cc || pid == 0x05c4))
        || (vid == 0x0f0d && pid == 0x005e)
        || (vid == 0x0f0d && pid == 0x00ee)
        || (vid == 0x1f4f && pid == 0x1002));
}

// --- decoders (verbatim MMBasic, nunstruct[n] -> gp[n], nunfoundc -> changed) -
static void checkpush(const uint8_t *report, uint16_t len, struct s_Buttons button,
    uint16_t set, uint16_t *b) {
    (void)len;
    if (button.index == 0xFF) {
        return;
    }
    if (button.code == 192) {
        if (report[button.index] > 192) {
            *b |= set;
        }
    } else if (button.code == 64) {
        if (report[button.index] < 64) {
            *b |= set;
        }
    } else if (button.code < 8) {
        if (report[button.index] & (1 << button.code)) {
            *b |= set;
        }
    } else if (button.code > 128 && button.code < 136) {
        if ((report[button.index] & (1 << button.code)) == 0) {
            *b |= set;
        }
    }
}

static void process_generic_gamepad(const uint8_t *report, uint16_t len, uint8_t n) {
    uint16_t b = 0;
    int i = 0;
    struct s_Gamepad Gamepad;
    if (MyGamepad.pid == gp[n].pid && MyGamepad.vid == gp[n].vid) {
        memcpy(&Gamepad, &MyGamepad, sizeof(struct s_Gamepad));
        goto process; // user specified decode
    }
    while (Gamepads[i].pid) {
        if (Gamepads[i].pid == gp[n].pid && Gamepads[i].vid == gp[n].vid) {
            break;
        }
        i++;
    }
    if (Gamepads[i].pid == 0) {
        return;
    }
    memcpy(&Gamepad, &Gamepads[i], sizeof(struct s_Gamepad));
process:;
    gp[n].type = GP_TYPE_GENERIC;
    checkpush(report, len, Gamepad.b_A, p_A, &b);
    checkpush(report, len, Gamepad.b_B, p_B, &b);
    checkpush(report, len, Gamepad.b_DOWN, p_DOWN, &b);
    checkpush(report, len, Gamepad.b_HOME, p_HOME, &b);
    checkpush(report, len, Gamepad.b_L2, p_L2, &b);
    checkpush(report, len, Gamepad.b_L, p_L, &b);
    checkpush(report, len, Gamepad.b_LEFT, p_LEFT, &b);
    checkpush(report, len, Gamepad.b_R2, p_R2, &b);
    checkpush(report, len, Gamepad.b_R, p_R, &b);
    checkpush(report, len, Gamepad.b_RIGHT, p_RIGHT, &b);
    checkpush(report, len, Gamepad.b_SELECT, p_SELECT, &b);
    checkpush(report, len, Gamepad.b_START, p_START, &b);
    checkpush(report, len, Gamepad.b_TOUCH, p_TOUCH, &b);
    checkpush(report, len, Gamepad.b_UP, p_UP, &b);
    checkpush(report, len, Gamepad.b_X, p_X, &b);
    checkpush(report, len, Gamepad.b_Y, p_Y, &b);
    if ((b ^ gp[n].x0) & gp[n].x1) {
        gp[n].changed = true;
    }
    gp[n].x0 = b;
}

static void process_xbox(const uint8_t *report, uint16_t len, uint8_t n) {
    gp[n].type = GP_TYPE_XBOX;
    uint16_t b = 0;
    if (len == 9) {
        if (report[0] & 0x10) { b |= 0x0400; }
        if (report[0] & 0x02) { b |= 0x0800; }
        if (report[0] & 0x08) { b |= 0x1000; }
        if (report[0] & 0x01) { b |= 0x2000; }
        if (report[1] & 0x08) { b |= 0x0002; }
        if (report[1] & 0x04) { b |= 0x0008; }
        if (report[1] & 0x10) { b |= 0x0004; }
        if (report[0] & 0x80) { b |= 0x0001; }
        if (report[0] & 0x40) { b |= 0x0010; }
        if (report[2] == 0x4) { b |= 0x20; }
        if (report[2] == 0x2) { b |= 0x40; }
        if (report[2] == 0x0) { b |= 0x80; }
        if (report[2] == 0x6) { b |= 0x100; }
        gp[n].ax = report[3];
        gp[n].ay = report[4];
        gp[n].Z = report[5];
        gp[n].C = report[6];
        gp[n].L = report[8];
        gp[n].R = report[7];
    }
    if ((b ^ gp[n].x0) & gp[n].x1) {
        gp[n].changed = true;
    }
    gp[n].x0 = b;
}

static void process_sony_ds3(const uint8_t *report, uint16_t len, uint8_t n) {
    (void)len;
    gp[n].type = GP_TYPE_PS3;
    uint16_t b = 0;
    if (report[3] & 0x08) { b |= 1; }
    if (report[2] & 0x08) { b |= 1 << 1; }
    if (report[4] & 0x01) { b |= 1 << 2; }
    if (report[2] & 0x01) { b |= 1 << 3; }
    if (report[3] & 0x04) { b |= 1 << 4; }
    if (report[2] & 0x40) { b |= 1 << 5; }
    if (report[2] & 0x20) { b |= 1 << 6; }
    if (report[2] & 0x10) { b |= 1 << 7; }
    if (report[2] & 0x80) { b |= 1 << 8; }
    if (report[3] & 0x02) { b |= 1 << 9; }
    if (report[3] & 0x10) { b |= 1 << 10; }
    if (report[3] & 0x20) { b |= 1 << 11; }
    if (report[3] & 0x80) { b |= 1 << 12; }
    if (report[3] & 0x40) { b |= 1 << 13; }
    if (report[3] & 0x01) { b |= 1 << 14; }
    gp[n].ax = report[6];
    gp[n].ay = report[7];
    gp[n].Z = report[8];
    gp[n].C = report[9];
    gp[n].L = report[18];
    gp[n].R = report[19];
    if ((b ^ gp[n].x0) & gp[n].x1) {
        gp[n].changed = true;
    }
    gp[n].x0 = b;
}

static void process_sony_ds4(const uint8_t *report, uint16_t len, uint8_t n) {
    uint8_t const report_id = report[0];
    report++;
    len--;
    if (report_id == 1 && len >= sizeof(sony_ds4_report_t)) {
        sony_ds4_report_t ds4_report;
        memcpy(&ds4_report, report, sizeof(ds4_report));
        gp[n].type = GP_TYPE_PS4;
        uint16_t b = 0;
        if (ds4_report.r1) { b |= 1; }
        if (ds4_report.option) { b |= 1 << 1; }
        if (ds4_report.ps) { b |= 1 << 2; }
        if (ds4_report.share) { b |= 1 << 3; }
        if (ds4_report.l1) { b |= 1 << 4; }
        if (ds4_report.dpad == 5) { b |= 1 << 5; }
        if (ds4_report.dpad == 3) { b |= 3 << 5; }
        if (ds4_report.dpad == 2) { b |= 1 << 6; }
        if (ds4_report.dpad == 1) { b |= 3 << 6; }
        if (ds4_report.dpad == 0) { b |= 1 << 7; }
        if (ds4_report.dpad == 6) { b |= 1 << 8; }
        if (ds4_report.dpad == 7) { b |= ((1 << 8) | (1 << 5)); }
        if (ds4_report.r2) { b |= 1 << 9; }
        if (ds4_report.triangle) { b |= 1 << 10; }
        if (ds4_report.circle) { b |= 1 << 11; }
        if (ds4_report.square) { b |= 1 << 12; }
        if (ds4_report.cross) { b |= 1 << 13; }
        if (ds4_report.l2) { b |= 1 << 14; }
        if (ds4_report.tpad) { b |= 1 << 15; }
        if (ds4_report.dpad == 4) { b |= 1 << 5; }
        gp[n].ax = ds4_report.x;
        gp[n].ay = ds4_report.y;
        gp[n].Z = ds4_report.z;
        gp[n].C = ds4_report.rz;
        gp[n].L = ds4_report.l2_trigger;
        gp[n].R = ds4_report.r2_trigger;
        memcpy((void *)gp[n].imu, ds4_report.gyro, 6 * sizeof(int16_t));
        if ((b ^ gp[n].x0) & gp[n].x1) {
            gp[n].changed = true;
        }
        gp[n].x0 = b;
    }
}

// --- glue: mount / report dispatch / umount --------------------------------
static int gp_find(uint8_t addr, uint8_t inst) {
    for (int n = 1; n < GP_NCHAN; n++) {
        if (gp[n].active && gp[n].addr == addr && gp[n].inst == inst) {
            return n;
        }
    }
    return -1;
}

void usb_gamepad_mount(uint8_t dev_addr, uint8_t instance, int slot1) {
    if (slot1 < 1 || slot1 >= GP_NCHAN) {
        return;
    }
    memset(&gp[slot1], 0, sizeof(gp[slot1]));
    gp[slot1].active = true;
    gp[slot1].addr = dev_addr;
    gp[slot1].inst = instance;
    gp[slot1].x1 = 0xFFFF;
    gp[slot1].y0 = 0xFF; // hat idle
    tuh_vid_pid_get(dev_addr, &gp[slot1].vid, &gp[slot1].pid);
}

void usb_gamepad_on_report(uint8_t dev_addr, uint8_t instance,
    const uint8_t *report, uint16_t len) {
    int n = gp_find(dev_addr, instance);
    if (n < 0) {
        return;
    }
    // Keep the raw report available for the "RAW" query (MMBasic HID[n].report).
    uint8_t cap = (uint8_t)(len > 64 ? 64 : len);
    gp[n].report[0] = cap;
    memcpy(&gp[n].report[1], report, cap);
    gp[n].report_len = cap;

    uint16_t vid = gp[n].vid, pid = gp[n].pid;
    if (is_sony_ds4(vid, pid)) {
        process_sony_ds4(report, len, (uint8_t)n);
    } else if (is_sony_ds3(vid, pid)) {
        process_sony_ds3(report, len, (uint8_t)n);
    } else if (is_xbox(vid, pid)) {
        process_xbox(report, len, (uint8_t)n);
    } else {
        process_generic_gamepad(report, len, (uint8_t)n);
    }
}

void usb_gamepad_on_umount(uint8_t dev_addr, uint8_t instance) {
    int n = gp_find(dev_addr, instance);
    if (n >= 0) {
        memset(&gp[n], 0, sizeof(gp[n]));
    }
}

void usb_gamepad_configure(uint16_t vid, uint16_t pid, const uint8_t *pairs) {
    // 16 (index, code) pairs, MMBasic order (see header).
    struct s_Buttons *b[16] = {
        &MyGamepad.b_R, &MyGamepad.b_START, &MyGamepad.b_HOME, &MyGamepad.b_SELECT,
        &MyGamepad.b_L, &MyGamepad.b_DOWN, &MyGamepad.b_RIGHT, &MyGamepad.b_UP,
        &MyGamepad.b_LEFT, &MyGamepad.b_R2, &MyGamepad.b_X, &MyGamepad.b_A,
        &MyGamepad.b_Y, &MyGamepad.b_B, &MyGamepad.b_L2, &MyGamepad.b_TOUCH,
    };
    MyGamepad.vid = vid;
    MyGamepad.pid = pid;
    for (int i = 0; i < 16; i++) {
        b[i]->index = pairs[i * 2];
        b[i]->code = pairs[i * 2 + 1];
    }
}

void usb_gamepad_set_mask(int chan, uint16_t mask) {
    if (chan >= 1 && chan < GP_NCHAN) {
        gp[chan].x1 = mask;
    }
}

// Resolve a query channel: 1..4 explicit, or 0 = first connected gamepad.
static int gp_resolve(int chan) {
    if (chan >= 1 && chan < GP_NCHAN) {
        return chan;
    }
    if (chan == 0) {
        for (int n = 1; n < GP_NCHAN; n++) {
            if (gp[n].active) {
                return n;
            }
        }
    }
    return -1;
}

int32_t usb_gamepad_query(int chan, int code) {
    if (code == GQ_SLOT) {
        int n = gp_resolve(chan);
        return n < 0 ? 0 : n;
    }
    int n = gp_resolve(chan);
    if (code == GQ_PRESENT) {
        return (n >= 0 && gp[n].active) ? 1 : 0;
    }
    if (n < 0 || !gp[n].active) {
        return (code == GQ_H) ? 0xFF : 0;
    }
    switch (code) {
        case GQ_LX: return gp[n].ax;
        case GQ_LY: return gp[n].ay;
        case GQ_RX: return gp[n].Z;
        case GQ_RY: return gp[n].C;
        case GQ_L: return gp[n].L;
        case GQ_R: return gp[n].R;
        case GQ_B: return gp[n].x0;
        case GQ_H: return gp[n].y0;
        case GQ_GX: return gp[n].imu[0];
        case GQ_GY: return gp[n].imu[1];
        case GQ_GZ: return gp[n].imu[2];
        case GQ_AX: return gp[n].imu[3];
        case GQ_AY: return gp[n].imu[4];
        case GQ_AZ: return gp[n].imu[5];
        case GQ_T: return (int32_t)gp[n].type;
        case GQ_CHANGED: {
            int32_t v = gp[n].changed ? 1 : 0;
            gp[n].changed = false;
            return v;
        }
        default: return 0;
    }
}

const uint8_t *usb_gamepad_raw(int chan, int *len) {
    int n = gp_resolve(chan);
    if (n < 0 || !gp[n].active) {
        *len = 0;
        return NULL;
    }
    *len = gp[n].report_len;
    return &gp[n].report[1];
}

#endif // MICROPY_HW_USB_HOST
