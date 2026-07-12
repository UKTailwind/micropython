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

// USB host (TinyUSB) glue for the Pico Computer 3. The RP2350's USB controller
// runs in host mode (USB-device disabled). tuh_task() is pumped from the
// MicroPython event hook (MICROPY_INTERNAL_EVENT_HOOK -> mp_usbh_task) whenever
// the runtime waits, e.g. at the REPL. This first cut just enumerates and prints
// mount/unmount diagnostics; HID/keyboard handling comes next.

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/ringbuf.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "tusb.h"
#include "pico/time.h"
#include "usb_touch.h" // USB multi-touch digitizer support (usb_touch.c)
#include "usb_mouse.h" // USB mouse support (usb_mouse.c)

// Active keyboard layout (+ its name, for the AltGr special cases), owned by
// usb_keyboard.c (the `keyboard` module).
extern const int *kbd_layout;
extern const char *kbd_layout_name;

// The REPL's input ring buffer (fed by the UART IRQ); the USB keyboard pushes
// translated keystrokes here too, so both drive the console.
extern ringbuf_t stdin_ringbuf;

static void kbd_repeat_check(void);
static void hid_poll(void);

static bool usbh_inited = false;
static repeating_timer_t usbh_wake_timer;

// --- HID device slots (ported verbatim from MMBasic's model) ----------------
// Four fixed slots addressed 1..4: 1=keyboard, 2=mouse, 3=gamepad, 4=touch.
// Reports are NOT re-armed in the receive callback (that's what broke multi-
// device use). Instead each slot is POLLED: report_timer counts up in the 1 ms
// timer below; when it reaches report_rate, hid_poll() issues ONE
// tuh_hid_receive_report and sets report_requested; the receive callback stores
// the report, dispatches it, and clears report_requested + resets report_timer.
// This is exactly MMBasic's hid_app_task / tuh_hid_report_received_cb scheme.
#define HID_NSLOTS 4
enum { HID_NONE = 0, HID_KBD = 1, HID_MOUSE = 2, HID_PAD = 3, HID_TOUCH = 4 };

typedef struct {
    volatile bool active;
    uint8_t addr;
    uint8_t inst;
    uint8_t type;                   // HID_*
    volatile bool report_requested; // a tuh_hid_receive_report is in flight
    volatile int report_timer;      // ms since last report (++ in the 1 ms timer)
    int report_rate;                // ms between report requests
    bool notfirsttime;              // keyboard: LED report has been sent once
    uint8_t sendlights;             // keyboard LED bitmap: 0x01 num, 0x02 caps, 0x04 scroll
} hid_slot_t;

static hid_slot_t hid_slots[HID_NSLOTS]; // [0]=slot 1 .. [3]=slot 4

// Fires every 1 ms: advances each active slot's report_timer (bounded, as
// MMBasic does) so hid_poll() knows when to request the next report, and wakes
// the core from WFE so the event hook keeps pumping tuh_task() during
// enumeration's timed steps. Runs in IRQ context — only touches plain counters.
static bool usbh_wake_cb(repeating_timer_t *t) {
    (void)t;
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (hid_slots[i].type != HID_NONE && hid_slots[i].report_timer < 10000) {
            hid_slots[i].report_timer++;
        }
    }
    return true;
}

void mp_usbh_init(void) {
    if (usbh_inited) {
        return;
    }
    tuh_init(0); // native controller, root-hub port 0
    add_repeating_timer_us(-1000, usbh_wake_cb, NULL, &usbh_wake_timer);
    usbh_inited = true;
}

void mp_usbh_task(void) {
    // Reentrancy guard: tuh_task() is NOT reentrant. It can be reached twice
    // because an enumeration mp_printf() goes through dupterm to the on-screen
    // console (Python), which runs the VM, which fires MICROPY_VM_HOOK_LOOP →
    // mp_usbh_task() again while the outer tuh_task() is still on the stack.
    // Re-entering there corrupts USB enumeration (notably a second device such
    // as a touch panel plugged in alongside the keyboard). Skip the nested call.
    static bool in_task = false;
    if (usbh_inited && !in_task) {
        in_task = true;
        tuh_task();
        hid_poll();       // MMBasic hid_app_task: request the next report per slot
        kbd_repeat_check();
        usb_touch_task(); // touch digitizer-init handshake + no-report watchdog
        in_task = false;
    }
}

// --- TinyUSB host callbacks (weak in TinyUSB; defined here) ----------------

// Schedule the registered Python USB-event callback (keyboard.on_usb_event),
// called as cb(True) on connect / cb(False) on disconnect. Runs from tuh_task()
// (thread context), so mp_sched_schedule is safe; a NULL/None callback or a
// full scheduler queue is silently ignored.
static void usbh_notify_event(bool connect) {
    mp_obj_t cb = MP_STATE_PORT(usbh_event_cb);
    if (cb != MP_OBJ_NULL && cb != mp_const_none) {
        mp_sched_schedule(cb, mp_obj_new_bool(connect));
    }
}

void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    mp_printf(&mp_plat_print, "USB: mounted addr=%u VID:PID=%04x:%04x\n",
        dev_addr, vid, pid);
    usbh_notify_event(true);
}

void tuh_umount_cb(uint8_t dev_addr) {
    mp_printf(&mp_plat_print, "USB: unmounted addr=%u\n", dev_addr);
    usbh_notify_event(false);
}

// --- HID keyboard -> REPL stdin --------------------------------------------

// HID modifier byte (report[0]) masks.
#define KBD_SHIFT (0x22) // left|right shift
#define KBD_CTRL  (0x11) // left|right ctrl

// Auto-repeat (typematic): HID keyboards only report on state change, so we
// synthesise repeats from the held key.
#define KBD_REPEAT_DELAY_MS (400)
#define KBD_REPEAT_RATE_MS  (40)

static uint8_t kbd_prev[6];       // keycodes from the previous report
static bool kbd_caps;             // caps-lock state
static bool kbd_num = true;       // num-lock state (on at boot, as MMBasic Option.numlock defaults)
static bool kbd_scroll;           // scroll-lock state
static uint8_t kbd_held;          // usage currently repeating (0 = none)
static uint8_t kbd_held_mods;
static uint32_t kbd_next_repeat;  // mp_hal_ticks_ms() of the next repeat

// Currently-held keys, MMBasic KeyDown[]: [0..5] = mapped key codes of the held
// keys, most recent first; [6] = modifier bitmap (1 L-Alt, 2 L-Ctrl, 4 L-GUI,
// 8 L-Shift, 16 R-Alt, 32 R-Ctrl, 64 R-GUI, 128 R-Shift). Written by the report
// callback, read by keyboard.keydown() (usb_keyboard.c) via usb_kbd_keydown().
static volatile int kbd_keydown[7];

// Push the current LED bitmap to a keyboard slot's physical LEDs, exactly as
// MMBasic does: a 1-byte OUTPUT report (report id 0). Called from the report
// callback (on a lock keypress) and from hid_poll() (once, at first poll).
static void kbd_set_leds(int slot) {
    if (slot < 0) {
        return;
    }
    tuh_hid_set_report(hid_slots[slot].addr, hid_slots[slot].inst, 0,
        HID_REPORT_TYPE_OUTPUT, &hid_slots[slot].sendlights, 1);
}

static void kbd_push(uint8_t c) {
    #if MICROPY_KBD_EXCEPTION
    if (c == mp_interrupt_char) {
        mp_sched_keyboard_interrupt();
        return;
    }
    #endif
    ringbuf_put(&stdin_ringbuf, c);
}

// AltGr (right-Alt) layout specials: {usage, code} pairs, terminated by usage 0.
// Verbatim from MMBasic APP_MapKeyToUsage (a code of 0 means "dead", as MMBasic).
static const uint8_t kbd_altgr_de[] = {0x24, 0x7b, 0x25, 0x5b, 0x26, 0x5d, 0x27, 0x7d,
                                       0x2d, 0x5c, 0x14, 0x40, 0x64, 124, 0x30, 126, 0};
static const uint8_t kbd_altgr_fr[] = {0x1f, 126, 0x20, 35, 0x21, 123, 0x22, 91,
                                       0x23, 124, 0x24, 96, 0x25, 92, 0x26, 94,
                                       0x27, 64, 0x2d, 93, 0x2e, 125, 0};
static const uint8_t kbd_altgr_es[] = {0x35, 92, 0x1e, 124, 0x08, 0, 0x1f, 64,
                                       0x20, 35, 0x21, 0, 0x2f, 91, 0x30, 93,
                                       0x31, 125, 0x34, 123, 0};
static const uint8_t kbd_altgr_be[] = {0x64, 92, 0x20, 35, 0x2f, 91, 0x30, 93,
                                       0x31, 96, 0x34, 39, 0x38, 126, 0};

// Map a HID usage + modifier byte to the key code MMBasic's KEYDOWN() reports:
// the layout-table value (ASCII for printing keys, 0x80.. for the cursor /
// function keys) with AltGr, Ctrl, num-lock and caps-lock applied. Faithful
// port of MMBasic APP_MapKeyToUsage (KeyboardMap.c). Returns 0 for "no code"
// (lock keys, out-of-table usages, dead AltGr combinations).
static int kbd_map_code(uint8_t usage, uint8_t mods) {
    if (usage == 0x39 || usage == 0x47 || usage == 0x53) {
        return 0; // caps/scroll/num lock
    }
    if (usage < 0x04 || usage > 0x64) {
        return 0;
    }
    if (mods & 0x40) { // right Alt (AltGr): layout-specific specials
        const uint8_t *t = NULL;
        if (kbd_layout_name[0] == 'D') {
            t = kbd_altgr_de;
        } else if (kbd_layout_name[0] == 'F') {
            t = kbd_altgr_fr;
        } else if (kbd_layout_name[0] == 'E') {
            t = kbd_altgr_es;
        } else if (kbd_layout_name[0] == 'B') {
            t = kbd_altgr_be;
        }
        for (; t && t[0]; t += 2) {
            if (t[0] == usage) {
                return t[1];
            }
        }
    }
    bool shift = mods & KBD_SHIFT;
    if (usage <= 0x1d && (mods & KBD_CTRL)) {
        return kbd_layout[usage * 2] - 96; // Ctrl-<letter> -> 1..26
    }
    if (usage >= 0x54 && usage <= 0x63) { // numeric keypad: num-lock selects the column
        return kbd_layout[usage * 2 + (kbd_num ? 0 : 1)];
    }
    if (usage <= 0x1d) { // letters: caps-lock XOR shift
        return kbd_layout[usage * 2 + ((kbd_caps ^ shift) ? 1 : 0)];
    }
    return kbd_layout[usage * 2 + (shift ? 1 : 0)];
}

// Schedule the registered Python key callback (keyboard.on_key) with the mapped
// key code. Called on each new keypress and on each synthesised auto-repeat, so
// the callback sees the same stream a console reader would. Runs in thread
// context (tuh_task / mp_usbh_task), so mp_sched_schedule is safe; a missing
// callback, an unmapped key, or a full scheduler queue is silently ignored.
static void kbd_notify_key(uint8_t usage, uint8_t mods) {
    mp_obj_t cb = MP_STATE_PORT(usbh_key_cb);
    if (cb != MP_OBJ_NULL && cb != mp_const_none) {
        int code = kbd_map_code(usage, mods);
        if (code) {
            mp_sched_schedule(cb, MP_OBJ_NEW_SMALL_INT(code));
        }
    }
}

// Translate one key (HID usage + modifiers) to bytes and push to stdin. `slot`
// is the keyboard's HID slot (for driving its lock-key LEDs), or -1.
static void kbd_key(uint8_t usage, uint8_t mods, int slot) {
    bool shift = mods & KBD_SHIFT;
    bool ctrl = mods & KBD_CTRL;
    // Lock keys: toggle the state, update the LED bitmap, and push it to the
    // keyboard's LEDs (exactly as MMBasic). Bits: 0x01 num, 0x02 caps, 0x04 scroll.
    if (usage == 0x39 || usage == 0x53 || usage == 0x47) {
        bool *state = (usage == 0x39) ? &kbd_caps : (usage == 0x53) ? &kbd_num : &kbd_scroll;
        uint8_t bit = (usage == 0x39) ? 0x02 : (usage == 0x53) ? 0x01 : 0x04;
        *state = !*state;
        if (slot >= 0) {
            if (*state) {
                hid_slots[slot].sendlights |= bit;
            } else {
                hid_slots[slot].sendlights &= ~bit;
            }
            kbd_set_leds(slot);
        }
        return;
    }
    // Numeric keypad: with num-lock off the digit/period keys act as the
    // navigation cluster (MMBasic behaviour). Redirect to the equivalent nav
    // usage so the VT100 switch below emits the right escape sequence.
    // Keypad-5 has no nav function and keeps typing '5' (as MMBasic's table).
    if (!kbd_num && usage >= 0x59 && usage <= 0x63) {
        //                          kp1   kp2   kp3   kp4  kp5  kp6   kp7   kp8   kp9   kp0   kp.
        static const uint8_t nav[] = {0x4d, 0x51, 0x4e, 0x50, 0, 0x4f, 0x4a, 0x52, 0x4b, 0x49, 0x4c};
        if (nav[usage - 0x59]) {
            usage = nav[usage - 0x59];
        }
    }
    // The table covers printing keys. Skip the caps-lock and editing/navigation
    // cluster (0x39, 0x49..0x52) so they fall to the VT100 switch below -- e.g.
    // Delete (0x4c) is 0x7f in the table, which readline/pye both read as
    // backspace; we want it to emit "\x1b[3~" (forward delete) instead.
    if (usage <= 0x64 && usage != 0x39 && !(usage >= 0x49 && usage <= 0x52)) {
        bool letter = (usage >= 0x04 && usage <= 0x1d);
        bool eff_shift = (letter && kbd_caps) ? !shift : shift;
        int v = kbd_layout[usage * 2 + (eff_shift ? 1 : 0)];
        if (v != 0) {
            if (ctrl && ((v | 0x20) >= 'a' && (v | 0x20) <= 'z')) {
                v &= 0x1f; // Ctrl-<letter> -> 0x01..0x1a
            }
            kbd_push((uint8_t)v);
            return;
        }
    }
    // Keys with no printing value -> control codes / VT100 sequences, so
    // readline/pye track them. (Table already covers enter/tab/space/etc., but
    // keep fallbacks in case a layout leaves them zero.)
    const char *seq = NULL;
    switch (usage) {
        case 0x28: seq = "\r"; break;      // enter
        case 0x29: seq = "\x1b"; break;    // escape
        case 0x2a: seq = "\x08"; break;    // backspace
        case 0x2b: seq = "\t"; break;      // tab
        case 0x4f: seq = "\x1b[C"; break;  // right
        case 0x50: seq = "\x1b[D"; break;  // left
        case 0x51: seq = "\x1b[B"; break;  // down
        case 0x52: seq = "\x1b[A"; break;  // up
        case 0x4a: seq = "\x1b[H"; break;  // home
        case 0x4d: seq = "\x1b[F"; break;  // end
        case 0x49: seq = "\x1b[2~"; break; // insert
        case 0x4c: seq = "\x1b[3~"; break; // delete
        case 0x4b: seq = "\x1b[5~"; break; // page up
        case 0x4e: seq = "\x1b[6~"; break; // page down
        default: return;
    }
    for (const char *p = seq; *p; p++) {
        kbd_push((uint8_t)*p);
    }
}

static void kbd_process_report(const uint8_t *r, uint16_t len, int slot) {
    if (len < 8) {
        return; // 8-byte boot report: [mods][reserved][keycode x6]
    }
    uint8_t mods = r[0];
    const uint8_t *keys = r + 2;
    for (int i = 0; i < 6; i++) {
        uint8_t k = keys[i];
        if (k <= 1) {
            continue; // 0 = none, 1 = roll-over error
        }
        bool was_down = false;
        for (int j = 0; j < 6; j++) {
            if (kbd_prev[j] == k) {
                was_down = true;
                break;
            }
        }
        if (!was_down) { // newly pressed
            kbd_key(k, mods, slot);
            kbd_notify_key(k, mods); // keyboard.on_key callback
            if (k != 0x39 && k != 0x53 && k != 0x47) { // don't auto-repeat lock keys
                kbd_held = k; // last new key wins the repeat
                kbd_held_mods = mods;
                kbd_next_repeat = mp_hal_ticks_ms() + KBD_REPEAT_DELAY_MS;
            }
        }
    }
    if (kbd_held) { // stop repeating once the held key is released
        bool still = false;
        for (int i = 0; i < 6; i++) {
            if (keys[i] == kbd_held) {
                still = true;
                break;
            }
        }
        if (!still) {
            kbd_held = 0;
        }
    }
    memcpy(kbd_prev, keys, 6);

    // Update the held-key table (MMBasic KeyDown[], read by keyboard.keydown()).
    // Keycodes 1..3 are HID error indicators (roll-over etc.) — MMBasic returns
    // without touching KeyDown on such a report, so we keep the previous state.
    int total = 0;
    for (int i = 0; i < 6; i++) {
        if (keys[i] > 0 && keys[i] < 4) {
            return;
        }
        if (keys[i]) {
            total++;
        }
    }
    for (int i = 0; i < 6; i++) {
        // Reverse report order, so keydown(1) is the most recently pressed key.
        uint8_t k = (i < total) ? keys[total - i - 1] : 0;
        kbd_keydown[i] = k ? kbd_map_code(k, mods) : 0;
    }
    kbd_keydown[6] = ((mods & 0x04) ? 1 : 0)     // left Alt
        | ((mods & 0x01) ? 2 : 0)                // left Ctrl
        | ((mods & 0x08) ? 4 : 0)                // left GUI
        | ((mods & 0x02) ? 8 : 0)                // left Shift
        | ((mods & 0x40) ? 16 : 0)               // right Alt
        | ((mods & 0x10) ? 32 : 0)               // right Ctrl
        | ((mods & 0x80) ? 64 : 0)               // right GUI
        | ((mods & 0x20) ? 128 : 0);             // right Shift
}

// keyboard.keydown(n) backend, MMBasic fun_keydown semantics: n=0 -> number of
// keys currently held, n=1..6 -> code of the nth held key (0 = none, 1 = most
// recent), n=7 -> modifier bitmap, n=8 -> lock bitmap (1 caps, 2 num, 4 scroll).
int usb_kbd_keydown(int n) {
    if (n == 8) {
        return (kbd_caps ? 1 : 0) | (kbd_num ? 2 : 0) | (kbd_scroll ? 4 : 0);
    }
    if (n >= 1) {
        return kbd_keydown[n - 1];
    }
    int count = 0;
    for (int i = 0; i < 6; i++) {
        if (kbd_keydown[i]) {
            count++;
        }
    }
    return count;
}

// Called from mp_usbh_task (thread context) to synthesise auto-repeat.
static void kbd_repeat_check(void) {
    if (kbd_held) {
        uint32_t now = mp_hal_ticks_ms();
        if ((int32_t)(now - kbd_next_repeat) >= 0) {
            kbd_key(kbd_held, kbd_held_mods, -1); // never a lock key -> no LED slot needed
            kbd_notify_key(kbd_held, kbd_held_mods); // on_key sees repeats, like console input
            kbd_next_repeat = now + KBD_REPEAT_RATE_MS;
        }
    }
}

static int hid_slot_find(uint8_t addr, uint8_t inst) {
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (hid_slots[i].active && hid_slots[i].addr == addr && hid_slots[i].inst == inst) {
            return i;
        }
    }
    return -1;
}

// MMBasic FindFreeSlot: keyboard->1, mouse->2, protocol-NONE (gamepad/touch)->
// 3 then 4, else the highest free slot. Returns 0-based index, or -1 if full.
static int hid_find_free_slot(uint8_t proto) {
    if (proto == HID_ITF_PROTOCOL_KEYBOARD && !hid_slots[0].active) {
        return 0;
    }
    if (proto == HID_ITF_PROTOCOL_MOUSE && !hid_slots[1].active) {
        return 1;
    }
    if (proto == HID_ITF_PROTOCOL_NONE) {
        if (!hid_slots[2].active) {
            return 2;
        }
        if (!hid_slots[3].active) {
            return 3;
        }
    }
    for (int i = HID_NSLOTS - 1; i >= 0; i--) {
        if (!hid_slots[i].active) {
            return i;
        }
    }
    return -1;
}

// MMBasic hid_app_task's report-request loop: for each active slot that has no
// request in flight and whose timer has reached its rate, issue exactly one
// tuh_hid_receive_report. On failure, allow a retry next cycle. Called from
// mp_usbh_task after tuh_task().
static void hid_poll(void) {
    for (int i = 0; i < HID_NSLOTS; i++) {
        if (!hid_slots[i].active || hid_slots[i].report_requested) {
            continue;
        }
        if (hid_slots[i].report_timer >= hid_slots[i].report_rate) {
            // First poll of a keyboard: push the initial LED state once (MMBasic).
            if (hid_slots[i].type == HID_KBD && !hid_slots[i].notfirsttime) {
                hid_slots[i].notfirsttime = true;
                kbd_set_leds(i);
            }
            hid_slots[i].report_requested = true;
            if (!tuh_hid_receive_report(hid_slots[i].addr, hid_slots[i].inst)) {
                hid_slots[i].report_requested = false;
                hid_slots[i].report_timer = 0;
            }
        }
    }
}

// --- HID class callbacks ---------------------------------------------------

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
    uint8_t const *desc_report, uint16_t desc_len) {
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    mp_printf(&mp_plat_print, "USB HID: addr=%u instance=%u protocol=%u\n",
        dev_addr, instance, proto);

    int slot = hid_find_free_slot(proto);
    if (slot == -1) {
        return; // no free slot — skip this interface quietly (as MMBasic does)
    }

    uint8_t type = HID_PAD; // default for an unrecognised protocol-NONE device
    if (proto == HID_ITF_PROTOCOL_KEYBOARD) {
        // NOTE: do NOT call tuh_hid_set_protocol(BOOT) here. TinyUSB already
        // activates boot protocol on boot-capable interfaces during enumeration
        // (MMBasic relies on exactly this). Issuing a control transfer from the
        // mount callback wedges EP0 and blocks any device enumerating behind
        // the keyboard — the "keyboard first = nothing else enumerates" bug.
        type = HID_KBD;
    } else if (proto == HID_ITF_PROTOCOL_MOUSE) {
        type = HID_MOUSE;
        usb_mouse_mount(dev_addr, instance, desc_report, desc_len);
    } else if (proto == HID_ITF_PROTOCOL_NONE) {
        // Multi-touch digitizers enumerate as protocol NONE with a Touch Screen
        // collection. probe_mount parses it and (if it's a touch panel) switches
        // to the report protocol + arms the bring-up handshake.
        if (usb_touch_probe_mount(dev_addr, instance, desc_report, desc_len)) {
            type = HID_TOUCH;
            // MMBasic routes touch to slot 4 (index 3) by preference.
            if (!(hid_slots[3].active && slot != 3)) {
                slot = 3;
            }
        }
    }

    hid_slots[slot].active = true;
    hid_slots[slot].addr = dev_addr;
    hid_slots[slot].inst = instance;
    hid_slots[slot].type = type;
    hid_slots[slot].report_requested = false;
    hid_slots[slot].report_rate = (type == HID_TOUCH) ? 5 : 20; // ms, as MMBasic
    // Staggered startup delay (MMBasic): first report request is issued this many
    // ms after mount, spreading device start-up and giving touch's bring-up
    // handshake time to complete before polling begins.
    hid_slots[slot].report_timer = -(10 + (slot + 2) * 500);
    hid_slots[slot].notfirsttime = false;
    // Seed the keyboard LED bitmap from the current lock states so the panel's
    // LEDs match on the first poll (MMBasic seeds from Option.capslock/numlock).
    hid_slots[slot].sendlights = (type == HID_KBD)
        ? ((kbd_num ? 0x01 : 0) | (kbd_caps ? 0x02 : 0) | (kbd_scroll ? 0x04 : 0))
        : 0;

    if (type == HID_TOUCH) {
        usb_touch_set_slot(slot + 1); // 1-based, for touch("X", 4)
    } else if (type == HID_MOUSE) {
        usb_mouse_set_slot(slot + 1); // 1-based, for mouse("X", 2)
    }
    static const char *const type_names[] = { "?", "keyboard", "mouse", "gamepad", "touch" };
    mp_printf(&mp_plat_print, "USB %s -> slot %d\n", type_names[type], slot + 1);
    // NOTE: no tuh_hid_receive_report here — hid_poll() issues it on the timer.
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    kbd_held = 0;
    int slot = hid_slot_find(dev_addr, instance);
    if (slot >= 0) {
        if (hid_slots[slot].type == HID_KBD) {
            // No keys can be held once the keyboard is gone (MMBasic clearrepeat).
            memset((void *)kbd_keydown, 0, sizeof(kbd_keydown));
            memset(kbd_prev, 0, sizeof(kbd_prev));
        }
        if (hid_slots[slot].type == HID_TOUCH) {
            usb_touch_on_umount(dev_addr, instance);
        } else if (hid_slots[slot].type == HID_MOUSE) {
            usb_mouse_on_umount(dev_addr, instance);
        }
        memset(&hid_slots[slot], 0, sizeof(hid_slots[slot]));
        hid_slots[slot].report_requested = true; // don't poll an empty slot
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
    uint8_t const *report, uint16_t len) {
    int slot = hid_slot_find(dev_addr, instance);
    if (slot < 0) {
        return; // no matching slot — discard (as MMBasic does)
    }
    uint8_t type = hid_slots[slot].type;
    if (type == HID_KBD) {
        kbd_process_report(report, len, slot);
    } else if (type == HID_TOUCH) {
        usb_touch_on_report(dev_addr, instance, report, len);
    } else if (type == HID_MOUSE) {
        usb_mouse_on_report(dev_addr, instance, report, len);
    }
    // Gamepad slots are polled but not yet decoded.
    // Clear the in-flight flag and reset the timer; hid_poll() re-requests when
    // the timer next reaches report_rate. Deliberately NO re-arm here.
    hid_slots[slot].report_requested = false;
    hid_slots[slot].report_timer = 0;
}

// GET_FEATURE / SET_FEATURE completions sequence the touch bring-up handshake.
void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance,
    uint8_t report_id, uint8_t report_type, uint16_t len) {
    (void)report_type;
    (void)len;
    usb_touch_get_complete(dev_addr, instance, report_id);
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance,
    uint8_t report_id, uint8_t report_type, uint16_t len) {
    (void)report_id;
    (void)report_type;
    (void)len;
    usb_touch_set_complete(dev_addr, instance);
}

#endif // MICROPY_HW_USB_HOST
