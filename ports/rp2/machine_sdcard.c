/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Damien P. George
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

// machine.SDCard: an SD card connected over SPI, exposed as a block device
// suitable for mounting with os.VfsFat().
//
// The low-level SPI/SD protocol is adapted from the MMBasic PicoMite SDCard
// driver (which is itself derived from ChaN's FatFs SPI sample), simplified for
// a single card on fixed, board-determined pins driven directly by the pico-sdk.

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/vfs.h"

#if MICROPY_PY_MACHINE_SDCARD

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

// Board configuration. The SD card lives on a fixed SPI bus and set of pins.
#if !defined(MICROPY_HW_SD_SPI_ID) || !defined(MICROPY_HW_SD_CS) || \
    !defined(MICROPY_HW_SD_SCK) || !defined(MICROPY_HW_SD_MOSI) || !defined(MICROPY_HW_SD_MISO)
#error "machine.SDCard requires MICROPY_HW_SD_SPI_ID, MICROPY_HW_SD_CS, MICROPY_HW_SD_SCK, MICROPY_HW_SD_MOSI and MICROPY_HW_SD_MISO to be defined by the board"
#endif

#define SD_SPI (MICROPY_HW_SD_SPI_ID == 0 ? spi0 : spi1)

// SPI clock during card identification must be 100-400 kHz; run fast afterwards.
#ifndef MICROPY_HW_SD_SPI_BAUD_SLOW
#define MICROPY_HW_SD_SPI_BAUD_SLOW (400 * 1000)
#endif
#ifndef MICROPY_HW_SD_SPI_BAUD_FAST
#define MICROPY_HW_SD_SPI_BAUD_FAST (12 * 1000 * 1000)
#endif

#define SD_BLOCK_SIZE (512)

// Disk status flags (subset of ChaN's diskio.h).
#define STA_NOINIT  (0x01)
#define STA_PROTECT (0x04)

// Card type flags.
#define CT_MMC   (0x01)
#define CT_SD1   (0x02)
#define CT_SD2   (0x04)
#define CT_SDC   (CT_SD1 | CT_SD2)
#define CT_BLOCK (0x08)

// MMC/SDC commands (ACMD<n> flagged with 0x80).
#define CMD0   (0)          // GO_IDLE_STATE
#define CMD1   (1)          // SEND_OP_COND
#define ACMD41 (41 | 0x80)  // SEND_OP_COND (SDC)
#define CMD8   (8)          // SEND_IF_COND
#define CMD9   (9)          // SEND_CSD
#define CMD12  (12)         // STOP_TRANSMISSION
#define CMD16  (16)         // SET_BLOCKLEN
#define CMD17  (17)         // READ_SINGLE_BLOCK
#define CMD18  (18)         // READ_MULTIPLE_BLOCK
#define ACMD23 (23 | 0x80)  // SET_WR_BLK_ERASE_COUNT (SDC)
#define CMD24  (24)         // WRITE_BLOCK
#define CMD25  (25)         // WRITE_MULTIPLE_BLOCK
#define CMD55  (55)         // APP_CMD
#define CMD58  (58)         // READ_OCR

extern const mp_obj_type_t machine_sdcard_type;

typedef struct _machine_sdcard_obj_t {
    mp_obj_base_t base;
    uint8_t status;      // STA_* flags
    uint8_t cardtype;    // CT_* flags
    uint32_t block_count;
} machine_sdcard_obj_t;

// There is only ever one SD card, on fixed pins, so use a singleton.
static machine_sdcard_obj_t machine_sdcard_obj = {
    .base = { &machine_sdcard_type },
    .status = STA_NOINIT,
    .cardtype = 0,
    .block_count = 0,
};

// Time (mp_hal_ticks_ms) of the most recent real block access. check() defers
// its liveness probe while the card has been used within SD_CHECK_MS, mirroring
// MMBasic's diskchecktimer being reset to DISKCHECKRATE by every SD access
// (CheckSDCard in FileIO.c) so the poll never disturbs an active transfer.
#define SD_CHECK_MS (500)
static uint32_t sd_last_activity;

// --- Low-level SPI helpers ------------------------------------------------

static inline uint8_t sd_xchg(uint8_t tx) {
    uint8_t rx;
    spi_write_read_blocking(SD_SPI, &tx, &rx, 1);
    return rx;
}

static inline void sd_write_multi(const uint8_t *buff, size_t len) {
    spi_write_blocking(SD_SPI, buff, len);
}

static inline void sd_read_multi(uint8_t *buff, size_t len) {
    spi_read_blocking(SD_SPI, 0xFF, buff, len);
}

static uint8_t sd_crc7(const uint8_t *message, size_t length) {
    const uint8_t poly = 0b10001001;
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc ^= message[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80u) ? ((crc << 1) ^ (poly << 1)) : (crc << 1);
        }
    }
    return crc >> 1;
}

// Wait until the card releases the bus (returns 0xFF). Returns true on success.
static bool sd_wait_ready(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        if (sd_xchg(0xFF) == 0xFF) {
            return true;
        }
    } while (!time_reached(deadline));
    return false;
}

static void sd_deselect(void) {
    gpio_put(MICROPY_HW_SD_CS, 1);
    sd_xchg(0xFF); // Dummy clock to force DO to hi-z.
}

static bool sd_select(void) {
    gpio_put(MICROPY_HW_SD_CS, 0);
    sd_xchg(0xFF); // Dummy clock to force DO enabled.
    if (sd_wait_ready(500)) {
        return true;
    }
    sd_deselect();
    return false;
}

// Receive a data packet (block).
static bool sd_rcvr_datablock(uint8_t *buff, size_t len) {
    uint8_t token;
    absolute_time_t deadline = make_timeout_time_ms(200);
    do {
        token = sd_xchg(0xFF);
    } while (token == 0xFF && !time_reached(deadline));
    if (token != 0xFE) {
        return false; // Not a valid data token.
    }
    sd_read_multi(buff, len);
    sd_xchg(0xFF); // Discard CRC.
    sd_xchg(0xFF);
    return true;
}

// Transmit a data packet (block). token 0xFD is the StopTran token.
static bool sd_xmit_datablock(const uint8_t *buff, uint8_t token) {
    if (!sd_wait_ready(500)) {
        return false;
    }
    sd_xchg(token);
    if (token != 0xFD) {
        sd_write_multi(buff, SD_BLOCK_SIZE);
        sd_xchg(0xFF); // Dummy CRC.
        sd_xchg(0xFF);
        if ((sd_xchg(0xFF) & 0x1F) != 0x05) {
            return false; // Data not accepted.
        }
    }
    return true;
}

// Send a command packet and return the R1 response.
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg) {
    uint8_t n, res;

    if (cmd & 0x80) {
        // ACMD<n> is the sequence CMD55, CMD<n>.
        cmd &= 0x7F;
        res = sd_send_cmd(CMD55, 0);
        if (res > 1) {
            return res;
        }
    }

    // Select the card, except when stopping a multiple block read.
    if (cmd != CMD12) {
        sd_deselect();
        if (!sd_select()) {
            return 0xFF;
        }
    }

    uint8_t command[6];
    command[0] = 0x40 | cmd;
    command[1] = arg >> 24;
    command[2] = arg >> 16;
    command[3] = arg >> 8;
    command[4] = arg;
    command[5] = (sd_crc7(command, 5) << 1) | 1;
    sd_write_multi(command, sizeof(command));

    if (cmd == CMD12) {
        sd_xchg(0xFF); // Skip a stuff byte when stopping a read.
    }
    n = 10; // Wait for a valid response (up to 10 attempts).
    do {
        res = sd_xchg(0xFF);
    } while ((res & 0x80) && --n);

    return res;
}

static void sd_spi_set_baud(uint32_t baud) {
    spi_set_baudrate(SD_SPI, baud);
}

// --- Card initialisation and I/O -----------------------------------------

static bool sd_read_sector_count(machine_sdcard_obj_t *self) {
    uint8_t csd[16];
    if (sd_send_cmd(CMD9, 0) != 0 || !sd_rcvr_datablock(csd, 16)) {
        sd_deselect();
        return false;
    }
    sd_deselect();
    if ((csd[0] >> 6) == 1) {
        // SDv2: C_SIZE is 22 bits, capacity = (C_SIZE + 1) * 512 KiB.
        uint32_t csize = csd[9] + ((uint32_t)csd[8] << 8) + ((uint32_t)(csd[7] & 63) << 16) + 1;
        self->block_count = csize << 10;
    } else {
        // SDv1 / MMC.
        uint8_t n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
        uint32_t csize = (csd[8] >> 6) + ((uint32_t)csd[7] << 2) + ((uint32_t)(csd[6] & 3) << 10) + 1;
        self->block_count = csize << (n - 9);
    }
    return true;
}

static bool sd_init_card(machine_sdcard_obj_t *self) {
    self->status = STA_NOINIT;
    self->cardtype = 0;
    self->block_count = 0;

    sd_spi_set_baud(MICROPY_HW_SD_SPI_BAUD_SLOW);
    sd_deselect();
    for (int i = 0; i < 10; i++) {
        sd_xchg(0xFF); // 80 dummy clocks with CS de-asserted.
    }

    uint8_t ty = 0;
    if (sd_send_cmd(CMD0, 0) == 1) { // Enter idle state.
        absolute_time_t deadline = make_timeout_time_ms(1000);
        if (sd_send_cmd(CMD8, 0x1AA) == 1) { // SDv2?
            uint8_t ocr[4];
            for (int i = 0; i < 4; i++) {
                ocr[i] = sd_xchg(0xFF);
            }
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) { // Supports 2.7-3.6V.
                while (!time_reached(deadline) && sd_send_cmd(ACMD41, 0x40000000)) {
                    ;
                }
                if (!time_reached(deadline) && sd_send_cmd(CMD58, 0) == 0) {
                    for (int i = 0; i < 4; i++) {
                        ocr[i] = sd_xchg(0xFF);
                    }
                    ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
                }
            }
        } else { // SDv1 or MMCv3.
            uint8_t cmd;
            if (sd_send_cmd(ACMD41, 0) <= 1) {
                ty = CT_SD1;
                cmd = ACMD41;
            } else {
                ty = CT_MMC;
                cmd = CMD1;
            }
            while (!time_reached(deadline) && sd_send_cmd(cmd, 0)) {
                ;
            }
            if (time_reached(deadline) || sd_send_cmd(CMD16, SD_BLOCK_SIZE) != 0) {
                ty = 0;
            }
        }
    }
    self->cardtype = ty;
    sd_deselect();

    if (ty == 0) {
        return false;
    }
    self->status &= ~STA_NOINIT;
    sd_spi_set_baud(MICROPY_HW_SD_SPI_BAUD_FAST);
    sd_read_sector_count(self);
    return true;
}

static bool sd_read_blocks(machine_sdcard_obj_t *self, uint8_t *buff, uint32_t sector, uint32_t count) {
    if (self->status & STA_NOINIT) {
        return false;
    }
    if (!(self->cardtype & CT_BLOCK)) {
        sector *= SD_BLOCK_SIZE; // Byte addressing for non-SDHC cards.
    }
    bool ok = false;
    if (count == 1) {
        if (sd_send_cmd(CMD17, sector) == 0 && sd_rcvr_datablock(buff, SD_BLOCK_SIZE)) {
            ok = true;
        }
    } else {
        if (sd_send_cmd(CMD18, sector) == 0) {
            ok = true;
            while (count--) {
                if (!sd_rcvr_datablock(buff, SD_BLOCK_SIZE)) {
                    ok = false;
                    break;
                }
                buff += SD_BLOCK_SIZE;
            }
            sd_send_cmd(CMD12, 0); // STOP_TRANSMISSION.
        }
    }
    sd_deselect();
    return ok;
}

static bool sd_write_blocks(machine_sdcard_obj_t *self, const uint8_t *buff, uint32_t sector, uint32_t count) {
    if (self->status & STA_NOINIT) {
        return false;
    }
    if (self->status & STA_PROTECT) {
        return false;
    }
    if (!(self->cardtype & CT_BLOCK)) {
        sector *= SD_BLOCK_SIZE;
    }
    bool ok = false;
    if (count == 1) {
        if (sd_send_cmd(CMD24, sector) == 0 && sd_xmit_datablock(buff, 0xFE)) {
            ok = true;
        }
    } else {
        if (self->cardtype & CT_SDC) {
            sd_send_cmd(ACMD23, count);
        }
        if (sd_send_cmd(CMD25, sector) == 0) {
            ok = true;
            while (count--) {
                if (!sd_xmit_datablock(buff, 0xFC)) {
                    ok = false;
                    break;
                }
                buff += SD_BLOCK_SIZE;
            }
            if (!sd_xmit_datablock(NULL, 0xFD)) { // STOP_TRAN token.
                ok = false;
            }
        }
    }
    sd_deselect();
    return ok;
}

// Lightweight liveness probe: READ_OCR (CMD58) and discard the 4-byte OCR.
// This is exactly what MMBasic's CheckSDCard uses (disk_ioctl(MMC_GET_OCR)) to
// tell whether the card is still there without disturbing any data. Returns
// true if the card answered. The MISO pull-up makes this fast (no long wait)
// when the card has been pulled out.
static bool sd_probe_alive(void) {
    bool ok = false;
    if (sd_send_cmd(CMD58, 0) == 0) {
        for (int i = 0; i < 4; i++) {
            sd_xchg(0xFF); // Consume the OCR bytes (R3 response).
        }
        ok = true;
    }
    sd_deselect();
    return ok;
}

// --- Python bindings ------------------------------------------------------

static void machine_sdcard_hw_init(void) {
    // Configure CS as a GPIO output (de-asserted) and the bus pins for SPI.
    gpio_init(MICROPY_HW_SD_CS);
    gpio_set_dir(MICROPY_HW_SD_CS, GPIO_OUT);
    gpio_put(MICROPY_HW_SD_CS, 1);
    spi_init(SD_SPI, MICROPY_HW_SD_SPI_BAUD_SLOW);
    spi_set_format(SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(MICROPY_HW_SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(MICROPY_HW_SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MICROPY_HW_SD_MISO, GPIO_FUNC_SPI);
    // A pull-up on MISO is essential: while no card is driving the line (during
    // identification and between transfers) it must idle high so wait_ready()
    // sees 0xFF. Match MMBasic's drive strength and input hysteresis too.
    gpio_pull_up(MICROPY_HW_SD_MISO);
    gpio_set_drive_strength(MICROPY_HW_SD_MOSI, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(MICROPY_HW_SD_SCK, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_input_hysteresis_enabled(MICROPY_HW_SD_MISO, true);
}

static mp_obj_t machine_sdcard_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    machine_sdcard_obj_t *self = &machine_sdcard_obj;
    machine_sdcard_hw_init();
    sd_init_card(self); // Best-effort; leaves STA_NOINIT set if no card present.
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t machine_sdcard_readblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    sd_last_activity = mp_hal_ticks_ms(); // Defer the next liveness probe.
    bool ok = sd_read_blocks(self, bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SD_BLOCK_SIZE);
    return MP_OBJ_NEW_SMALL_INT(ok ? 0 : -MP_EIO);
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_sdcard_readblocks_obj, machine_sdcard_readblocks);

static mp_obj_t machine_sdcard_writeblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    sd_last_activity = mp_hal_ticks_ms(); // Defer the next liveness probe.
    bool ok = sd_write_blocks(self, bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SD_BLOCK_SIZE);
    return MP_OBJ_NEW_SMALL_INT(ok ? 0 : -MP_EIO);
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_sdcard_writeblocks_obj, machine_sdcard_writeblocks);

static mp_obj_t machine_sdcard_ioctl(mp_obj_t self_in, mp_obj_t cmd_in, mp_obj_t arg_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t cmd = mp_obj_get_int(cmd_in);
    switch (cmd) {
        case MP_BLOCKDEV_IOCTL_INIT:
            return MP_OBJ_NEW_SMALL_INT(sd_init_card(self) ? 0 : -MP_EIO);
        case MP_BLOCKDEV_IOCTL_DEINIT:
            self->status |= STA_NOINIT;
            return MP_OBJ_NEW_SMALL_INT(0);
        case MP_BLOCKDEV_IOCTL_SYNC:
            return MP_OBJ_NEW_SMALL_INT(0);
        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return MP_OBJ_NEW_SMALL_INT((self->status & STA_NOINIT) ? -MP_EIO : (mp_int_t)self->block_count);
        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(SD_BLOCK_SIZE);
        default:
            return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_sdcard_ioctl_obj, machine_sdcard_ioctl);

// present() -> True if a card is initialised.
static mp_obj_t machine_sdcard_present(mp_obj_t self_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(!(self->status & STA_NOINIT));
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_sdcard_present_obj, machine_sdcard_present);

// check() -> True if the card is (still) present, False if it has just gone.
// Mirrors MMBasic's CheckSDCard: only meaningful once the card is initialised;
// the probe is deferred while the card has been used within SD_CHECK_MS so it
// never interrupts active I/O. When the probe fails the card is marked
// uninitialised (STA_NOINIT) so the caller knows to unmount and a later
// re-insert is handled by reinit(). A background poller calls this ~2x/second.
static mp_obj_t machine_sdcard_check(mp_obj_t self_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->status & STA_NOINIT) {
        return mp_const_false; // Not initialised: nothing to verify.
    }
    if (mp_hal_ticks_ms() - sd_last_activity < SD_CHECK_MS) {
        return mp_const_true; // Used recently: defer, assume still present.
    }
    if (sd_probe_alive()) {
        return mp_const_true;
    }
    // The card stopped answering: it has been removed. Mark it uninitialised so
    // the next access (or reinit()) re-identifies whatever is inserted next.
    self->status = STA_NOINIT;
    self->cardtype = 0;
    self->block_count = 0;
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_sdcard_check_obj, machine_sdcard_check);

// reinit() -> True if a card is now present and was (re)initialised. Used by
// the background poller to detect a freshly inserted card while unmounted; the
// MISO pull-up keeps this cheap when no card is in the slot.
static mp_obj_t machine_sdcard_reinit(mp_obj_t self_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    bool ok = sd_init_card(self);
    if (ok) {
        sd_last_activity = mp_hal_ticks_ms();
    }
    return mp_obj_new_bool(ok);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_sdcard_reinit_obj, machine_sdcard_reinit);

// info() -> (capacity_in_bytes, block_size) or None if not initialised.
static mp_obj_t machine_sdcard_info(mp_obj_t self_in) {
    machine_sdcard_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->status & STA_NOINIT) {
        return mp_const_none;
    }
    mp_obj_t tuple[2] = {
        mp_obj_new_int_from_ull((uint64_t)self->block_count * SD_BLOCK_SIZE),
        MP_OBJ_NEW_SMALL_INT(SD_BLOCK_SIZE),
    };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_sdcard_info_obj, machine_sdcard_info);

static const mp_rom_map_elem_t machine_sdcard_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_present),      MP_ROM_PTR(&machine_sdcard_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_check),        MP_ROM_PTR(&machine_sdcard_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_reinit),       MP_ROM_PTR(&machine_sdcard_reinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_info),         MP_ROM_PTR(&machine_sdcard_info_obj) },
    // Block device protocol.
    { MP_ROM_QSTR(MP_QSTR_readblocks),   MP_ROM_PTR(&machine_sdcard_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks),  MP_ROM_PTR(&machine_sdcard_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl),        MP_ROM_PTR(&machine_sdcard_ioctl_obj) },
};
static MP_DEFINE_CONST_DICT(machine_sdcard_locals_dict, machine_sdcard_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_sdcard_type,
    MP_QSTR_SDCard,
    MP_TYPE_FLAG_NONE,
    make_new, machine_sdcard_make_new,
    locals_dict, &machine_sdcard_locals_dict
    );

#endif // MICROPY_PY_MACHINE_SDCARD
