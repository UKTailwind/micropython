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

// USB mass-storage (flash drive) host for the Pico Computer 3. A drive plugged
// into the host port enumerates through TinyUSB's msch class driver, which
// calls tuh_msc_mount_cb/umount_cb (defined here, weak in TinyUSB) once it has
// already read LUN 0's capacity during enumeration -- unlike the SD card
// (a bare SPI card with no card-detect signal, see pcsd.py's poller), USB
// gives a definitive connect/disconnect event, so no polling is needed here.
//
// tuh_msc_read10/write10 are non-blocking and callback-based, but
// machine.USBDrive's readblocks/writeblocks (like machine.SDCard's) must
// return synchronously, so usb_msc_read10/write10 below pump tuh_task() in a
// loop until the completion callback fires or a timeout elapses. This is safe
// to call directly (rather than through mp_usbh_task()) because readblocks/
// writeblocks run to completion inside a single VM call, never re-entering
// the idle event hook that also pumps tuh_task() -- the same non-reentrancy
// argument mp_usbh.c makes for its own guard, just via a different caller.

#include "py/runtime.h"
#include "py/mphal.h"

#if MICROPY_HW_USB_HOST

#include "tusb.h"
#include "usb_msc.h"

static volatile bool msc_mounted = false;
static uint8_t msc_dev_addr;

// Set by msc_xfer_complete_cb; polled by msc_wait(). xfer_ok is only
// meaningful once xfer_done is true. Single-core, cooperatively scheduled --
// the completion callback fires synchronously from within the tuh_task() call
// msc_wait() itself makes, so plain volatiles are enough (as elsewhere in this
// port's USB host code).
static volatile bool xfer_done;
static volatile bool xfer_ok;

// A read10/write10 completes after several USB transfers (CBW, data, CSW),
// each pumped from tuh_task(); generous for a slow/hub-connected drive
// without hanging forever on one that dropped off the bus mid-command (in
// which case TinyUSB never calls the completion callback at all).
#define MSC_XFER_TIMEOUT_MS 3000

void usb_msc_init(void) {
    // Nothing to prime ahead of time -- all state is set from the mount
    // callbacks below, exactly like usb_cdc's per-interface state.
}

// Called as cb(True) on mount / cb(False) on unmount -- mp_sched_schedule
// always calls its target with exactly one argument, so the callback (see
// pcusb.py) must take one positional parameter, matching keyboard's
// on_usb_event(connect) and usb_cdc's on_change(idx) conventions elsewhere
// in this port.
static void notify_change(bool connected) {
    mp_obj_t cb = MP_STATE_PORT(usb_msc_change_cb);
    if (cb != MP_OBJ_NULL && cb != mp_const_none) {
        mp_sched_schedule(cb, mp_obj_new_bool(connected));
    }
}

// --- TinyUSB MSC host callbacks (weak in TinyUSB; defined here) ------------

void tuh_msc_mount_cb(uint8_t dev_addr) {
    msc_dev_addr = dev_addr;
    msc_mounted = true;
    mp_printf(&mp_plat_print, "USB drive: mounted addr=%u lun0 %lu x %lu bytes\n",
        dev_addr,
        (unsigned long)tuh_msc_get_block_count(dev_addr, 0),
        (unsigned long)tuh_msc_get_block_size(dev_addr, 0));
    notify_change(true);
}

void tuh_msc_umount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    msc_mounted = false;
    mp_printf(&mp_plat_print, "USB drive: unmounted addr=%u\n", dev_addr);
    notify_change(false);
}

// --- API for machine.USBDrive ------------------------------------------

bool usb_msc_present(void) {
    return msc_mounted && tuh_msc_mounted(msc_dev_addr);
}

uint32_t usb_msc_block_count(void) {
    return usb_msc_present() ? tuh_msc_get_block_count(msc_dev_addr, 0) : 0;
}

uint32_t usb_msc_block_size(void) {
    return usb_msc_present() ? tuh_msc_get_block_size(msc_dev_addr, 0) : 0;
}

static bool msc_xfer_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const *cb_data) {
    (void)dev_addr;
    xfer_ok = (cb_data->csw->status == 0);
    xfer_done = true;
    return true;
}

// Pump tuh_task() until the transfer completes, the drive disappears, or the
// timeout elapses.
static bool msc_wait(void) {
    uint32_t start = mp_hal_ticks_ms();
    while (!xfer_done) {
        tuh_task();
        if (!usb_msc_present()) {
            return false; // unplugged mid-transfer
        }
        if (mp_hal_ticks_ms() - start > MSC_XFER_TIMEOUT_MS) {
            return false;
        }
    }
    return xfer_ok;
}

bool usb_msc_read10(uint32_t lba, uint16_t count, uint8_t *buf) {
    if (!usb_msc_present()) {
        return false;
    }
    xfer_done = false;
    if (!tuh_msc_read10(msc_dev_addr, 0, buf, lba, count, msc_xfer_complete_cb, 0)) {
        return false;
    }
    return msc_wait();
}

bool usb_msc_write10(uint32_t lba, uint16_t count, const uint8_t *buf) {
    if (!usb_msc_present()) {
        return false;
    }
    xfer_done = false;
    if (!tuh_msc_write10(msc_dev_addr, 0, buf, lba, count, msc_xfer_complete_cb, 0)) {
        return false;
    }
    return msc_wait();
}

void usb_msc_set_change_cb(mp_obj_t cb) {
    MP_STATE_PORT(usb_msc_change_cb) = cb;
}

#endif // MICROPY_HW_USB_HOST
