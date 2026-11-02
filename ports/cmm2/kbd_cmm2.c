/*
 * The keyboard on the Colour Maximite 2 G2: the back end of the Pico Computer 3's keyboard module (ports/rp2's
 * usb_keyboard.c and kbd_backend.c, used unchanged), in place of its USB host's HID decoding (kbd_decode.c). The CMM2's
 * firmware decodes the keyboard itself, with OPTION KEYBOARD's layout, so keyboard.keymap() is kept but not used.
 *
 * Keys: the console's (the USB keyboard's and the serial port's), taken by cmm2_kbd_pump() from the VM's hook and while
 * stdin waits. Each goes into stdin_ringbuf through kbd_push(), and to keyboard.on_key()'s handler, as the PC3's USB
 * pump does. Ctrl-C is the firmware's break key, so it comes from break_take(): kbd_push() makes it a KeyboardInterrupt
 * while code runs, or puts it in the queue for the REPL's line editor.
 *
 * keydown(n) is MMBasic's KEYDOWN, from the firmware's (keys_held): the same codes as the PC3's.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/runtime.h"
#include "py/ringbuf.h"

#if MICROPY_HW_USB_HOST

#include "cmm2.h"

#define CHAR_CTRL_C (3)

extern void kbd_push(uint8_t c);
extern void kbd_backend_on_key(int code);

static uint8_t stdin_buf[256];
ringbuf_t stdin_ringbuf = { stdin_buf, sizeof(stdin_buf), 0, 0 };

void cmm2_kbd_pump(void) {
    int c;
    while (ringbuf_free(&stdin_ringbuf) > 0 && (c = cmm2->con_getc()) >= 0) {
        kbd_push((uint8_t)c);
        kbd_backend_on_key(c);
    }
    if (cmm2->break_take()) {
        kbd_push(CHAR_CTRL_C);
    }
}

int usb_kbd_keydown(int n) {
    int k[7], held = cmm2->keys_held(k);
    if (n == 0) {
        return held;
    }
    if (n <= 7) {
        return k[n - 1];
    }
    return 0;                               // keydown(8), the lock keys: the firmware keeps them to itself
}

// num lock: the firmware's (OPTION KEYBOARD's); the PC3's per-keyboard setting has nothing to act on here
int usb_kbd_get_numlock(void) {
    return 0;
}
void usb_kbd_set_numlock(int on) {
    (void)on;
}
void usb_kbd_numlock_pref(uint16_t vid, uint16_t pid, int on) {
    (void)vid;
    (void)pid;
    (void)on;
}
int usb_kbd_numlock_led(void) {
    return 0;
}
uint32_t usb_kbd_id(void) {
    return 0;                               // (kbd_id() is None: the firmware does not say which keyboard it has)
}
uint16_t usb_kbd_desc(const uint8_t **p) {
    static const uint8_t none[1];
    *p = none;
    return 0;
}

#endif // MICROPY_HW_USB_HOST
