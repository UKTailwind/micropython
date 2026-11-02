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

/* The keys MMBasic gives as codes of its own (from the USB keyboard, and from a serial terminal's sequences): to stdin
   as the VT100 sequences the PC3's keyboard gives (kbd_decode.c), which its programs read (pcfm, pye, readline): the
   arrows, the editing keys and F1-F12, shifted too. keyboard.on_key() and keydown() still see MMBasic's codes, as on
   the PC3. Any other byte goes as it is, as there. */
static const char *key_sequence(int c) {
    static const char *const nav[] = {                          // 0x80-0x89: up, down, left, right, insert, -, home,
        "\x1b[A", "\x1b[B", "\x1b[D", "\x1b[C", "\x1b[2~", NULL,    // end, page up, page down
        "\x1b[H", "\x1b[F", "\x1b[5~", "\x1b[6~"
    };
    static const char *const fkey[] = {                         // 0x91-0x9C: F1-F12
        "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS", "\x1b[15~", "\x1b[17~", "\x1b[18~", "\x1b[19~", "\x1b[20~",
        "\x1b[21~", "\x1b[23~", "\x1b[24~"
    };
    static const char *const shift_fkey[] = {                   // 0xB1-0xBC: shift F1-F12
        "\x1b[1;2P", "\x1b[1;2Q", "\x1b[1;2R", "\x1b[1;2S", "\x1b[15;2~", "\x1b[17;2~", "\x1b[18;2~",
        "\x1b[19;2~", "\x1b[20;2~", "\x1b[21;2~", "\x1b[23;2~", "\x1b[24;2~"
    };
    if (c >= 0x80 && c <= 0x89) {
        return nav[c - 0x80];
    }
    if (c == 0x7F) {
        return "\x1b[3~";                       // delete (MMBasic's, as the PC3's Delete key)
    }
    if (c >= 0x91 && c <= 0x9C) {
        return fkey[c - 0x91];
    }
    if (c >= 0xB1 && c <= 0xBC) {
        return shift_fkey[c - 0xB1];
    }
    return NULL;
}

void cmm2_kbd_pump(void) {
    int c;
    while (ringbuf_free(&stdin_ringbuf) >= 10 && (c = cmm2->con_getc()) >= 0) {
        const char *seq = key_sequence(c);
        if (seq) {
            while (*seq) {
                kbd_push((uint8_t)*seq++);
            }
        } else {
            kbd_push((uint8_t)c);
        }
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
