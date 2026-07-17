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

// The HID keyboard decoder, moved VERBATIM from mp_usbh.c so the PC
// emulator can share it (see kbd_decode.h). Faithful port of MMBasic's
// keyboard handling. The lock-key LED update is the one edit: it now goes
// through kbd_backend_set_leds() instead of touching hid_slots directly.

#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/ringbuf.h"
// The interrupt character (usually 3, Ctrl-C). A bare extern rather than
// shared/runtime/interrupt_char.h: that header's mp_hal_set_interrupt_char
// prototype conflicts with the unix port's, and rp2's mphal already declares
// everything else.
extern int mp_interrupt_char;

#if MICROPY_HW_USB_HOST

#include "kbd_decode.h"

// The REPL's input ring buffer; translated keystrokes are pushed here.
extern ringbuf_t stdin_ringbuf;

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
        *state = !*state;
        kbd_backend_set_leds(slot, kbd_led_bitmap());
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

void kbd_process_report(const uint8_t *r, uint16_t len, int slot) {
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
void kbd_repeat_check(void) {
    if (kbd_held) {
        uint32_t now = mp_hal_ticks_ms();
        if ((int32_t)(now - kbd_next_repeat) >= 0) {
            kbd_key(kbd_held, kbd_held_mods, -1); // never a lock key -> no LED slot needed
            kbd_notify_key(kbd_held, kbd_held_mods); // on_key sees repeats, like console input
            kbd_next_repeat = now + KBD_REPEAT_RATE_MS;
        }
    }
}

// Lock-state bitmap in LED order (0x01 num, 0x02 caps, 0x04 scroll) -- the
// value a keyboard's sendlights byte is seeded with on mount.
uint8_t kbd_led_bitmap(void) {
    return (uint8_t)((kbd_num ? 0x01 : 0) | (kbd_caps ? 0x02 : 0) | (kbd_scroll ? 0x04 : 0));
}

// A keyboard went away (MMBasic clearrepeat): stop the auto-repeat...
void kbd_stop_repeat(void) {
    kbd_held = 0;
}

// ...and no keys can still be held.
void kbd_clear_state(void) {
    memset((void *)kbd_keydown, 0, sizeof(kbd_keydown));
    memset(kbd_prev, 0, sizeof(kbd_prev));
}

#endif // MICROPY_HW_USB_HOST
