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

#include "kbd_decode.h" // the shared HID keyboard decoder
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

// --- HID keyboard decode: shared code in kbd_decode.c ----------------------

// Push the current LED bitmap to a keyboard slot's physical LEDs, exactly as
// MMBasic does: a 1-byte OUTPUT report (report id 0). Called from
// kbd_backend_set_leds (below, on a lock keypress) and from hid_poll()
// (once, at first poll).
static void kbd_set_leds(int slot) {
    if (slot < 0) {
        return;
    }
    tuh_hid_set_report(hid_slots[slot].addr, hid_slots[slot].inst, 0,
        HID_REPORT_TYPE_OUTPUT, &hid_slots[slot].sendlights, 1);
}

// The decoder's lock-key seam: record the new bitmap and push it out.
void kbd_backend_set_leds(int slot, uint8_t leds) {
    if (slot < 0) {
        return;
    }
    hid_slots[slot].sendlights = leds;
    kbd_set_leds(slot);
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
    hid_slots[slot].sendlights = (type == HID_KBD) ? kbd_led_bitmap() : 0;

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
    kbd_stop_repeat();
    int slot = hid_slot_find(dev_addr, instance);
    if (slot >= 0) {
        if (hid_slots[slot].type == HID_KBD) {
            // No keys can be held once the keyboard is gone (MMBasic clearrepeat).
            kbd_clear_state();
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
