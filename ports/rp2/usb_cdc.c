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

// USB CDC (serial) host for the Pico Computer 3. TinyUSB delivers a USB-serial
// adapter's bytes via tuh_cdc_rx_cb; we drain them into a per-interface receive
// ring buffer that the UART-like `USBSerial` object reads. Connect/disconnect,
// line coding and DTR/RTS follow MMBasic's Serial.c CDC handling. tuh_task()
// (pumped from the MicroPython event hook in mp_usbh.c) drives all of this --
// unlike HID, CDC needs no manual report polling.

#include "py/runtime.h"
#include "py/ringbuf.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "tusb.h"
#include "usb_cdc.h"

#define CDC_RX_BUFSIZE 512

typedef struct {
    volatile bool mounted;
    bool open;              // configured by Python (re-apply on reconnect)
    uint32_t baud;
    uint8_t bits;
    uint8_t cdc_parity;     // CDC_LINE_CODING_PARITY_*
    uint8_t stop;           // 1 or 2
    ringbuf_t rx;
    uint8_t rx_store[CDC_RX_BUFSIZE];
} cdc_t;

static cdc_t cdc[USB_CDC_MAX];
static bool cdc_inited = false;

// The connect/disconnect callback root pointer (usb_cdc_change_cb) is registered
// in usb_cdc_mod.c (a QSTR-scanned file); used here via MP_STATE_PORT. It is
// fired on any connect/disconnect as cb(index). The index is a small int
// (allocation-free), so scheduling it from the tuh_task context is safe; the
// handler queries USBSerial(index).connected() for the new state.

void usb_cdc_init(void) {
    if (cdc_inited) {
        return;
    }
    for (int i = 0; i < USB_CDC_MAX; i++) {
        cdc[i].rx.buf = cdc[i].rx_store;
        cdc[i].rx.size = CDC_RX_BUFSIZE;
        cdc[i].rx.iget = cdc[i].rx.iput = 0;
    }
    cdc_inited = true;
}

static void apply_line_coding(int idx) {
    if (!tuh_cdc_mounted(idx)) {
        return;
    }
    cdc_line_coding_t coding = {
        .bit_rate = cdc[idx].baud,
        .stop_bits = (cdc[idx].stop == 2) ? CDC_LINE_CODING_STOP_BITS_2 : CDC_LINE_CODING_STOP_BITS_1,
        .parity = cdc[idx].cdc_parity,
        .data_bits = cdc[idx].bits,
    };
    // Vendor bridges (FTDI/CP210x) may reject the ACM line-coding request; fall
    // back to just the baud rate, exactly as MMBasic does.
    if (!tuh_cdc_set_line_coding(idx, &coding, NULL, 0)) {
        tuh_cdc_set_baudrate(idx, cdc[idx].baud, NULL, 0);
    }
    tuh_cdc_set_control_line_state(idx,
        CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, NULL, 0);
}

static void notify_change(int idx) {
    mp_obj_t cb = MP_STATE_PORT(usb_cdc_change_cb);
    if (cb != MP_OBJ_NULL && cb != mp_const_none) {
        mp_sched_schedule(cb, MP_OBJ_NEW_SMALL_INT(idx));
    }
}

// --- TinyUSB CDC host callbacks (weak in TinyUSB; defined here) -------------
void tuh_cdc_mount_cb(uint8_t idx) {
    if (idx >= USB_CDC_MAX) {
        return;
    }
    usb_cdc_init();
    cdc[idx].mounted = true;
    // Fresh connection: start with an empty receive buffer.
    cdc[idx].rx.iget = cdc[idx].rx.iput = 0;
    if (cdc[idx].open) {
        apply_line_coding(idx); // re-assert the Python-chosen settings
    }
    mp_printf(&mp_plat_print, "USB serial connected on port %u\n", idx);
    notify_change(idx);
}

void tuh_cdc_umount_cb(uint8_t idx) {
    if (idx >= USB_CDC_MAX) {
        return;
    }
    cdc[idx].mounted = false;
    // Keep cdc[idx].open + settings so a reconnect resumes transparently
    // (MMBasic keeps the port "open" across a replug).
    mp_printf(&mp_plat_print, "USB serial disconnected on port %u\n", idx);
    notify_change(idx);
}

void tuh_cdc_rx_cb(uint8_t idx) {
    if (idx >= USB_CDC_MAX) {
        return;
    }
    usb_cdc_init();
    uint8_t buf[64];
    uint32_t n;
    while ((n = tuh_cdc_read(idx, buf, sizeof(buf))) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            // Drop the newest byte on overflow (the reader must keep up or use
            // a larger buffer) -- ringbuf_put is a no-op that returns -1 here.
            ringbuf_put(&cdc[idx].rx, buf[i]);
        }
    }
}

// --- API for the USBSerial stream object ------------------------------------
bool usb_cdc_connected(int idx) {
    return (idx >= 0 && idx < USB_CDC_MAX) && cdc[idx].mounted && tuh_cdc_mounted(idx);
}

void usb_cdc_open(int idx, uint32_t baud, uint8_t bits, int parity, uint8_t stop) {
    if (idx < 0 || idx >= USB_CDC_MAX) {
        return;
    }
    usb_cdc_init();
    cdc[idx].baud = baud;
    cdc[idx].bits = bits;
    cdc[idx].stop = stop;
    cdc[idx].cdc_parity = (parity < 0) ? CDC_LINE_CODING_PARITY_NONE
        : (parity == 1) ? CDC_LINE_CODING_PARITY_ODD : CDC_LINE_CODING_PARITY_EVEN;
    cdc[idx].open = true;
    apply_line_coding(idx); // no-op if nothing is plugged in yet
}

void usb_cdc_close(int idx) {
    if (idx >= 0 && idx < USB_CDC_MAX) {
        cdc[idx].open = false;
    }
}

int usb_cdc_any(int idx) {
    if (idx < 0 || idx >= USB_CDC_MAX) {
        return 0;
    }
    return ringbuf_avail(&cdc[idx].rx);
}

int usb_cdc_read(int idx, uint8_t *buf, size_t n) {
    if (idx < 0 || idx >= USB_CDC_MAX) {
        return 0;
    }
    size_t i = 0;
    int c;
    while (i < n && (c = ringbuf_get(&cdc[idx].rx)) >= 0) {
        buf[i++] = (uint8_t)c;
    }
    return (int)i;
}

int usb_cdc_write_avail(int idx) {
    if (!usb_cdc_connected(idx)) {
        return 0;
    }
    return (int)tuh_cdc_write_available(idx);
}

int usb_cdc_write(int idx, const uint8_t *buf, size_t n) {
    if (!usb_cdc_connected(idx)) {
        return 0;
    }
    uint32_t wrote = tuh_cdc_write(idx, buf, n);
    tuh_cdc_write_flush(idx);
    return (int)wrote;
}

bool usb_cdc_txdone(int idx) {
    // Best effort: nothing queued means the write FIFO has drained.
    return usb_cdc_connected(idx) && tuh_cdc_write_flush(idx);
}

void usb_cdc_set_change_cb(mp_obj_t cb) {
    MP_STATE_PORT(usb_cdc_change_cb) = cb;
}

#endif // MICROPY_HW_USB_HOST
