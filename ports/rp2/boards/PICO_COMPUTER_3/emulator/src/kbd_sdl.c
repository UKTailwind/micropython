/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * (Licence text as per ports/rp2/hdmi.c.)
 */

// The PC-emulator keyboard: SDL key events synthesised into 8-byte HID boot
// reports and fed to the firmware's own decoder (kbd_decode.c) -- possible
// because SDL scancodes ARE USB HID usage codes. Layouts (keymap("UK")),
// lock keys, auto-repeat, keydown(), on_key and Ctrl-C therefore behave
// exactly as on the machine, driven by identical code.
//
// Also provides the `_emukbd` module: the REPL bridge (emuboot registers a
// dupterm stream whose read() drains the same stdin ring buffer the USB
// keyboard fills on hardware) and an inject() hook the test suite uses to
// press keys without a window.

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <termios.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/ringbuf.h"

#if MICROPY_HW_USB_HOST

#include "../../../../kbd_decode.h"

extern int mp_interrupt_char; // unix_mphal.c (tracks mp_hal_set_interrupt_char)

// The stdin ring buffer the decoder pushes translated keystrokes into (the
// rp2 port defines this in its mphalport; the unix port has no equivalent,
// so the emulator owns one).
static uint8_t stdin_buf[260];
ringbuf_t stdin_ringbuf = { stdin_buf, sizeof(stdin_buf), 0, 0 };

// mp_interrupt_char is provided by unix_mphal.c, tracking
// mp_hal_set_interrupt_char: -1 at the REPL prompt (Ctrl-C flows to readline
// as byte 3 and cancels the line), 3 during execution (the decoder schedules
// a KeyboardInterrupt) -- the same contract as the machine.

// No physical lock LEDs to drive.
void kbd_backend_set_leds(int slot, uint8_t leds) {
    (void)slot;
    (void)leds;
}

// The keyboard module's num-lock surface. On the machine these live in
// mp_usbh.c, where they also remember the setting per USB keyboard and push the
// LED report that makes a compact keyboard drop its embedded keypad. Here there
// is no USB keyboard to identify or light up -- SDL delivers the host's keys --
// so only the decoder's own num-lock state is real, which is enough for the
// keypad digit/navigation remap to behave as it does on the board. kbd_id()
// returning 0 tells pcconfig there is no keyboard to save a setting against.
int usb_kbd_get_numlock(void) {
    return kbd_led_bitmap() & 0x01;
}

void usb_kbd_set_numlock(int on) {
    kbd_set_numlock(on);
}

void usb_kbd_numlock_pref(uint16_t vid, uint16_t pid, int on) {
    (void)vid;
    (void)pid;
    (void)on;
}

uint32_t usb_kbd_id(void) {
    return 0;
}

uint16_t usb_kbd_desc(const uint8_t **p) {
    *p = NULL; // SDL, not USB: there is no report descriptor to show
    return 0;
}

int usb_kbd_numlock_led(void) {
    return 1; // no descriptor to read, so the board's default for "unknown"
}

// --- SDL keys -> HID boot reports -------------------------------------------

static uint8_t sdl_held[6]; // pressed usages, report order (0 = free)

// SDL_Keymod -> HID modifier byte (bit0 LCtrl, 1 LShift, 2 LAlt, 3 LGui,
// 4 RCtrl, 5 RShift, 6 RAlt, 7 RGui). SDL: LSHIFT=1, RSHIFT=2, LCTRL=0x40,
// RCTRL=0x80, LALT=0x100, RALT=0x200, LGUI=0x400, RGUI=0x800.
static uint8_t mods_from_sdl(int m) {
    return (uint8_t)(((m & 0x0040) ? 0x01 : 0) | ((m & 0x0001) ? 0x02 : 0)
        | ((m & 0x0100) ? 0x04 : 0) | ((m & 0x0400) ? 0x08 : 0)
        | ((m & 0x0080) ? 0x10 : 0) | ((m & 0x0002) ? 0x20 : 0)
        | ((m & 0x0200) ? 0x40 : 0) | ((m & 0x0800) ? 0x80 : 0));
}

static void send_report(uint8_t mods) {
    uint8_t r[8] = { mods, 0, sdl_held[0], sdl_held[1], sdl_held[2],
                     sdl_held[3], sdl_held[4], sdl_held[5] };
    kbd_process_report(r, 8, -1);
}

// One key transition (usage = HID usage id == SDL scancode). Called from the
// SDL thread; also from _emukbd.inject() for the tests.
static void kbd_transition(int usage, int down, uint8_t mods) {
    if (usage < 4 || usage > 0xE7) {
        return;
    }
    if (usage >= 0xE0) {
        // Modifier keys live in the mods byte, not the key array; SDL's mod
        // state (passed in) already reflects them. Report the state change.
        send_report(mods);
        return;
    }
    if (down) {
        for (int i = 0; i < 6; i++) {
            if (sdl_held[i] == usage) {
                return; // already held
            }
        }
        for (int i = 0; i < 6; i++) {
            if (sdl_held[i] == 0) {
                sdl_held[i] = (uint8_t)usage;
                break; // >6 keys: dropped, as a boot keyboard would
            }
        }
    } else {
        for (int i = 0; i < 6; i++) {
            if (sdl_held[i] == usage) {
                sdl_held[i] = 0;
            }
        }
    }
    send_report(mods);
}

// Entry points for the SDL thread (hdmi_sdl.c's event loop).
void pc3emu_kbd_sdl_key(int scancode, int down, int sdl_mods) {
    kbd_transition(scancode, down, mods_from_sdl(sdl_mods));
}

void pc3emu_kbd_tick(void) {
    kbd_repeat_check();
}

// The window is the keyboard: when the scanout stops (hdmi.deinit(), e.g. a
// screen() mode change), any key release still queued in the dying window's
// event loop is lost -- so treat it as a keyboard unplug, exactly as the
// hardware does (MMBasic clearrepeat). Without this, the Enter that
// submitted screen(...) keeps auto-repeating in the new mode.
void pc3emu_kbd_reset(void) {
    memset(sdl_held, 0, sizeof(sdl_held));
    kbd_stop_repeat();
    kbd_clear_state();
}

// --- fd 0 becomes the machine console ---------------------------------------
//
// On the machine, sys.stdin IS the console: USB keyboard and serial input
// arrive through one stream, so autosave()/pye read both. The unix port's
// sys.stdin is the raw terminal fd, which the window keyboard can never
// reach. console_pipe() (called once at boot by emuboot) replaces fd 0 with
// a pipe fed by a merger thread: window keystrokes (the decoder's ring
// buffer) + the real terminal, one byte stream. Everything that reads
// stdin -- the REPL, autosave, pye -- then sees the whole console.

static int con_real = -1;          // the saved real stdin (terminal side)
static int con_pipe_w = -1;        // write end of the fd-0 pipe
static struct termios con_tio;     // terminal state to restore at exit
static bool con_tio_saved = false;
static pthread_mutex_t con_wlock = PTHREAD_MUTEX_INITIALIZER;

// Serialised write to the console pipe (feeder thread bytes and window
// pastes must not interleave mid-paste).
static void con_write(const uint8_t *b, size_t n) {
    pthread_mutex_lock(&con_wlock);
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(con_pipe_w, b + off, n - off);
        if (w <= 0) {
            break;
        }
        off += (size_t)w;
    }
    pthread_mutex_unlock(&con_wlock);
}

// Paste text into the console as keyboard input (window Ctrl-V; also
// _emukbd.paste for tests). LF and CRLF become CR -- what Enter sends -- so
// the REPL, autosave() and pye see paste and typing identically.
void pc3emu_kbd_paste(const char *txt) {
    size_t len = strlen(txt);
    uint8_t *buf = malloc(len ? len : 1);
    if (buf == NULL) {
        return;
    }
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        char c = txt[i];
        if (c == '\r' && txt[i + 1] == '\n') {
            continue; // CRLF: the LF that follows emits the CR
        }
        buf[n++] = (c == '\n') ? '\r' : (uint8_t)c;
    }
    if (con_pipe_w >= 0) {
        con_write(buf, n);
    } else {
        // No console pipe (test mode): best-effort into the ring buffer.
        for (size_t i = 0; i < n; i++) {
            ringbuf_put(&stdin_ringbuf, buf[i]);
        }
    }
    free(buf);
}

static void con_restore_tty(void) {
    if (con_tio_saved) {
        tcsetattr(con_real, TCSANOW, &con_tio);
    }
}

static void *con_feeder(void *arg) {
    (void)arg;
    bool wake_sent = false;
    for (;;) {
        // Window keystrokes (already interrupt-filtered by the decoder).
        int c;
        while ((c = ringbuf_get(&stdin_ringbuf)) >= 0) {
            uint8_t b = (uint8_t)c;
            con_write(&b, 1);
        }
        // A scheduled KeyboardInterrupt can't reach a program blocked in a
        // stdin read (the hardware's stdin loop pumps events; a posix read
        // can't). Nudge the reader with a NUL so the pending exception is
        // delivered; the byte is discarded along with the aborted read.
        if (MP_STATE_MAIN_THREAD(mp_pending_exception) != MP_OBJ_NULL) {
            if (!wake_sent) {
                uint8_t nul = 0;
                con_write(&nul, 1);
                wake_sent = true;
            }
        } else {
            wake_sent = false;
        }
        // The real terminal (poll doubles as the loop's 20 ms pace).
        if (con_real >= 0) {
            struct pollfd pfd = { con_real, POLLIN, 0 };
            if (poll(&pfd, 1, 20) > 0) {
                uint8_t b;
                ssize_t n = read(con_real, &b, 1);
                if (n <= 0) {
                    // Terminal EOF: one Ctrl-D, then window-only input.
                    uint8_t eot = 4;
                    con_write(&eot, 1);
                    con_real = -1;
                } else if (mp_interrupt_char >= 0 && b == (uint8_t)mp_interrupt_char) {
                    // Terminal Ctrl-C during execution: schedule the
                    // interrupt, exactly as the machine's UART IRQ does.
                    mp_sched_keyboard_interrupt();
                } else {
                    con_write(&b, 1);
                }
            }
        } else {
            usleep(20000);
        }
    }
    return NULL;
}

// console_pipe() -- install the merged console on fd 0. Once, at boot.
static mp_obj_t emukbd_console_pipe(void) {
    if (con_pipe_w >= 0) {
        return mp_const_none; // already installed
    }
    int p[2];
    if (pipe(p) != 0) {
        mp_raise_OSError(EPIPE);
    }
    con_real = dup(0);
    con_pipe_w = p[1];
    dup2(p[0], 0);
    close(p[0]);
    if (isatty(con_real) && tcgetattr(con_real, &con_tio) == 0) {
        con_tio_saved = true;
        atexit(con_restore_tty);
        // Raw mode on the REAL terminal: the REPL's raw-mode call now acts
        // on the pipe, so set the tty up front (restored at exit).
        struct termios t = con_tio;
        t.c_iflag &= ~(unsigned)(ICRNL | IXON);
        t.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(con_real, TCSANOW, &t);
    }
    pthread_t th;
    pthread_create(&th, NULL, con_feeder, NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(emukbd_console_pipe_obj, emukbd_console_pipe);

// --- the _emukbd module ------------------------------------------------------

// read() -> one byte from the keyboard's stdin stream, or None. emuboot's
// dupterm stream calls this so REPL input flows from the window.
static mp_obj_t emukbd_read(void) {
    int c = ringbuf_get(&stdin_ringbuf);
    if (c < 0) {
        return mp_const_none;
    }
    byte b = (byte)c;
    return mp_obj_new_bytes(&b, 1);
}
static MP_DEFINE_CONST_FUN_OBJ_0(emukbd_read_obj, emukbd_read);

// any() -> bytes waiting (cheap poll for the dupterm stream).
static mp_obj_t emukbd_any(void) {
    return MP_OBJ_NEW_SMALL_INT(ringbuf_avail(&stdin_ringbuf));
}
static MP_DEFINE_CONST_FUN_OBJ_0(emukbd_any_obj, emukbd_any);

// inject(usage, mods=0, down=True) -- press/release a key programmatically,
// exactly as if the SDL window had reported it. For the test suite.
static mp_obj_t emukbd_inject(size_t n_args, const mp_obj_t *args) {
    int usage = mp_obj_get_int(args[0]);
    int mods = (n_args > 1) ? mp_obj_get_int(args[1]) : 0;
    int down = (n_args > 2) ? mp_obj_is_true(args[2]) : 1;
    kbd_transition(usage, down, (uint8_t)mods);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(emukbd_inject_obj, 1, 3, emukbd_inject);

// paste(text) -- feed text into the console as keyboard input (the window's
// Ctrl-V uses the same path with the system clipboard).
static mp_obj_t emukbd_paste(mp_obj_t txt_in) {
    pc3emu_kbd_paste(mp_obj_str_get_str(txt_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(emukbd_paste_obj, emukbd_paste);

// tick() -- run the auto-repeat check (the SDL thread does this itself; the
// test suite calls it to pass time deterministically).
static mp_obj_t emukbd_tick(void) {
    kbd_repeat_check();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(emukbd_tick_obj, emukbd_tick);

static const mp_rom_map_elem_t emukbd_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__emukbd) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&emukbd_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&emukbd_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_console_pipe), MP_ROM_PTR(&emukbd_console_pipe_obj) },
    { MP_ROM_QSTR(MP_QSTR_paste), MP_ROM_PTR(&emukbd_paste_obj) },
    { MP_ROM_QSTR(MP_QSTR_inject), MP_ROM_PTR(&emukbd_inject_obj) },
    { MP_ROM_QSTR(MP_QSTR_tick), MP_ROM_PTR(&emukbd_tick_obj) },
};
static MP_DEFINE_CONST_DICT(emukbd_module_globals, emukbd_module_globals_table);

const mp_obj_module_t emukbd_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&emukbd_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__emukbd, emukbd_module);

#endif // MICROPY_HW_USB_HOST
