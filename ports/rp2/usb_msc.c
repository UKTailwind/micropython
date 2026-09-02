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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// USB mass-storage host for the Pico Computer 3: the TinyUSB MSC glue behind
// usbdrive.Drive. Ported from MMBasic's C: drive (PicoMite misc/SDCard.c),
// whose shape is worth keeping because it was measured:
//
//   * TinyUSB's per-transfer length is a uint16_t, so a SCSI READ(10)/WRITE(10)
//     carries at most 65535 bytes - 127 blocks of 512, 15 of 4096. Anything
//     bigger is a chain of chunks, each submitted after the last completes.
//   * Every command is three bulk transfers (CBW, data, CSW), each completing
//     through the interrupt and tuh_task(). That is about a millisecond per
//     command however small the data, so a one-block read costs as much as a
//     sixty-four block one costs in overhead; FatFS's whole-sector reads come
//     through here multi-block, clipped at the cluster, and do well.
//   * A transfer waits by pumping the host stack. The pump is the port's own
//     mp_usbh_task(), so the keyboard keeps being polled through a long copy.
//     tuh_task() is not reentrant, so a transfer asked for from inside it is
//     refused (-MP_EBUSY) rather than deadlocked.
//   * A command the drive fails is followed by REQUEST SENSE (which clears the
//     drive's check condition - the first command after a plug-in often earns
//     one) and retried, four times as MMBasic does; a chunk has a one second
//     timeout; a drive pulled mid-chain fails the chain with -MP_ENODEV.
//
// One drive at a time (MMBasic's scope), LUN 0. TinyUSB has already run
// TEST UNIT READY and READ CAPACITY when tuh_msc_mount_cb fires (msc_host.c's
// config chain), so the geometry is valid inside the callback.

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "tusb.h"
#include "pico/time.h"
#include "usb_msc.h"

#define MSC_CHUNK_TIMEOUT_MS   1000   // per command, as MMBasic
#define MSC_RETRIES            4      // per chunk, as MMBasic
#define MSC_SUBMIT_BACKOFF_US  200    // after a refused submit, as MMBasic

typedef struct {
    volatile bool mounted;
    uint8_t dev_addr;
    uint8_t lun;
    uint32_t block_count;
    uint32_t block_size;
    uint16_t max_blocks;      // per command: 65535 / block_size
} msc_drive_t;

static msc_drive_t drive;

// The command in flight: set by the completion callback (tuh_task context).
static volatile bool xfer_done;
static volatile bool xfer_ok;

// REQUEST SENSE lands here; TinyUSB wants a 4-byte aligned buffer of its own.
static TU_ATTR_ALIGNED(4) uint8_t sense_buf[32];

// The mount/unmount callback root pointer (usb_msc_change_cb) is registered in
// usb_msc_mod.c (a QSTR-scanned file). The argument is a bool (allocation-
// free), so scheduling it from the tuh_task context is safe.
static void notify_change(bool connected) {
    mp_obj_t cb = MP_STATE_PORT(usb_msc_change_cb);
    if (cb != MP_OBJ_NULL && cb != mp_const_none) {
        mp_sched_schedule(cb, mp_obj_new_bool(connected));
    }
}

// Why the notification is NOT scheduled from the TinyUSB callbacks directly:
// the callbacks run inside tuh_task(), and the port's mount messages are
// printed from there through dupterm, which runs the VM, which runs the
// scheduler - so a callback scheduled by tuh_msc_mount_cb executed while
// tuh_mount_cb was still printing, i.e. inside the stack, where the block
// device rightly refuses (EBUSY) and FatFS reports EIO. The first build did
// exactly that. So the callbacks only record the event, and usb_msc_task()
// - called by mp_usbh_task() after tuh_task() has returned - schedules it.
// +1 connect, -1 disconnect, 0 nothing pending.
static volatile int8_t pending_change;

void usb_msc_task(void) {
    int8_t p = pending_change;
    if (p != 0) {
        pending_change = 0;
        notify_change(p > 0);
    }
}

// --- TinyUSB MSC host callbacks (weak in TinyUSB; defined here) ------------

void tuh_msc_mount_cb(uint8_t dev_addr) {
    if (drive.mounted) {
        usb_defer_printf("USB drive: second drive (addr=%u) ignored - one at a time\n", dev_addr);
        return;
    }
    uint32_t count = tuh_msc_get_block_count(dev_addr, 0);
    uint32_t size = tuh_msc_get_block_size(dev_addr, 0);
    if (count == 0 || size == 0 || size > 4096) {
        // A card reader with no card, or something we cannot address.
        usb_defer_printf("USB drive: addr=%u reports no usable medium\n", dev_addr);
        return;
    }
    drive.dev_addr = dev_addr;
    drive.lun = 0;
    drive.block_count = count;
    drive.block_size = size;
    drive.max_blocks = (uint16_t)(65535u / size);
    xfer_done = true;
    drive.mounted = true;
    usb_defer_printf("USB drive: addr=%u, %u blocks of %u bytes (%u MB)\n",
        dev_addr, (unsigned)count, (unsigned)size,
        (unsigned)(((uint64_t)count * size) >> 20));
    pending_change = 1;
}

void tuh_msc_umount_cb(uint8_t dev_addr) {
    if (drive.mounted && dev_addr == drive.dev_addr) {
        drive.mounted = false;
        // A chain waiting on this drive sees mounted go false and fails.
        xfer_ok = false;
        xfer_done = true;
        usb_defer_printf("USB drive removed\n");
        pending_change = -1;
    }
}

// --- Transfers ---------------------------------------------------------------

static bool msc_complete_cb(uint8_t dev_addr, const tuh_msc_complete_data_t *cb_data) {
    (void)dev_addr;
    xfer_ok = (cb_data->csw->status == MSC_CSW_STATUS_PASSED);
    xfer_done = true;
    return true;
}

// Pump the stack until the command in flight completes. 0, -MP_ENODEV if the
// drive went away, -MP_ETIMEDOUT past the deadline, -MP_EIO if the drive
// failed the command.
static int msc_wait(uint64_t deadline) {
    while (!xfer_done) {
        mp_usbh_task();
        if (!drive.mounted) {
            return -MP_ENODEV;
        }
        if (time_us_64() > deadline) {
            return -MP_ETIMEDOUT;
        }
    }
    return xfer_ok ? 0 : -MP_EIO;
}

// Wait for the interface to be idle (the previous command's CSW may still be
// in flight). Same return convention.
static int msc_wait_ready(uint64_t deadline) {
    while (!tuh_msc_ready(drive.dev_addr)) {
        mp_usbh_task();
        if (!drive.mounted) {
            return -MP_ENODEV;
        }
        if (time_us_64() > deadline) {
            return -MP_ETIMEDOUT;
        }
    }
    return 0;
}

// Clear a check condition after a failed command: REQUEST SENSE, result ignored.
static void msc_request_sense(void) {
    uint64_t deadline = time_us_64() + (uint64_t)MSC_CHUNK_TIMEOUT_MS * 1000;
    if (msc_wait_ready(deadline) != 0) {
        return;
    }
    xfer_done = false;
    xfer_ok = false;
    if (tuh_msc_request_sense(drive.dev_addr, drive.lun, sense_buf, msc_complete_cb, 0)) {
        msc_wait(deadline);
    }
}

// One chunk of at most max_blocks, with retries.
static int msc_chunk(bool write, uint32_t lba, uint16_t blocks, uint8_t *buf) {
    int rc = -MP_EIO;
    for (int attempt = 0; attempt < MSC_RETRIES; attempt++) {
        uint64_t deadline = time_us_64() + (uint64_t)MSC_CHUNK_TIMEOUT_MS * 1000;
        rc = msc_wait_ready(deadline);
        if (rc != 0) {
            if (rc == -MP_ENODEV) {
                return rc;
            }
            continue;
        }
        xfer_done = false;
        xfer_ok = false;
        bool submitted = write
            ? tuh_msc_write10(drive.dev_addr, drive.lun, buf, lba, blocks, msc_complete_cb, 0)
            : tuh_msc_read10(drive.dev_addr, drive.lun, buf, lba, blocks, msc_complete_cb, 0);
        if (!submitted) {
            // The stack refused (an endpoint still busy): back off and try again.
            xfer_done = true;
            busy_wait_us_32(MSC_SUBMIT_BACKOFF_US);
            rc = -MP_EIO;
            continue;
        }
        rc = msc_wait(deadline);
        if (rc == 0 || rc == -MP_ENODEV) {
            return rc;
        }
        if (rc == -MP_EIO) {
            msc_request_sense();
        }
    }
    return rc;
}

static int msc_transfer(bool write, uint32_t lba, uint32_t count, uint8_t *buf) {
    if (!drive.mounted) {
        return -MP_ENODEV;
    }
    if (mp_usbh_in_task()) {
        return -MP_EBUSY;
    }
    if (count == 0) {
        return 0;
    }
    if (lba >= drive.block_count || count > drive.block_count - lba) {
        return -MP_EINVAL;
    }
    while (count) {
        uint16_t blocks = (count > drive.max_blocks) ? drive.max_blocks : (uint16_t)count;
        int rc = msc_chunk(write, lba, blocks, buf);
        if (rc != 0) {
            return rc;
        }
        buf += (size_t)blocks * drive.block_size;
        lba += blocks;
        count -= blocks;
    }
    return 0;
}

int usb_msc_read(uint32_t lba, uint32_t count, uint8_t *buf) {
    return msc_transfer(false, lba, count, buf);
}

int usb_msc_write(uint32_t lba, uint32_t count, const uint8_t *buf) {
    return msc_transfer(true, lba, count, (uint8_t *)buf);
}

// --- Surface for the module --------------------------------------------------

bool usb_msc_present(void) {
    return drive.mounted && tuh_msc_mounted(drive.dev_addr);
}

uint32_t usb_msc_block_count(void) {
    return drive.mounted ? drive.block_count : 0;
}

uint32_t usb_msc_block_size(void) {
    return drive.mounted ? drive.block_size : 0;
}

void usb_msc_vid_pid(uint16_t *vid, uint16_t *pid) {
    *vid = 0;
    *pid = 0;
    if (drive.mounted) {
        tuh_vid_pid_get(drive.dev_addr, vid, pid);
    }
}

void usb_msc_set_change_cb(mp_obj_t cb) {
    MP_STATE_PORT(usb_msc_change_cb) = cb;
}

#endif // MICROPY_HW_USB_HOST
