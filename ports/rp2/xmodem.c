// XMODEM file transfer over the console UART for the Pico Computer 3.
//
// A faithful port of MMBasic's XModem.c (Geoff Graham / Peter Mather): the
// classic 128-byte XMODEM with the additive checksum on receive and
// checksum-or-CRC on transmit, driven straight off the console UART hardware.
//
// While a transfer runs the console UART RX interrupt is disabled so raw bytes
// reach the protocol instead of the REPL, and output goes straight to the UART
// (never through the dupterm/HDMI console). The transfer workers therefore never
// raise -- they return an error string so the caller can always re-enable the
// IRQ first, then raise.
//
//   import xmodem
//   xmodem.recv("/sd/prog.py")   # then start an XMODEM *send* in the terminal
//   xmodem.send("/sd/prog.py")   # then start an XMODEM *receive* in the terminal

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/stream.h"
#include "extmod/vfs.h"

#if MICROPY_HW_ENABLE_UART_REPL

#include "hardware/uart.h"
#include "hardware/irq.h"
#include "pico/time.h"

// Protocol constants (from MMBasic XModem.c).
#define SOH   0x01  // start of 128-byte block
#define EOT   0x04  // end of transmission
#define ACK   0x06  // acknowledge
#define NAK   0x15  // not acknowledge
#define CAN   0x18  // cancel
#define CRC16 'C'   // request CRC-16 mode
#define PAD   0x1a

#define XPACKET_SIZE   128
#define X_BLOCK_SIZE   128
#define X_BUF_SIZE     (X_BLOCK_SIZE + 6)
#define PACKET_TIMEOUT 3000000  // 3 s
#define DLY_1S         1000000
#define MAXRETRANS     25
#define XMAXRETRANS    25
#define FIFOSIZE       1024

// CRC-16 CCITT table (from MMBasic XModem.c).
static const unsigned short crc16_ccitt_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

static unsigned short crc16_ccitt(const unsigned char *buf, int len) {
    unsigned short crc = 0;
    for (int i = 0; i < len; i++) {
        crc = (crc << 8) ^ crc16_ccitt_table[((crc >> 8) ^ buf[i]) & 0xFF];
    }
    return crc;
}

// --- raw console-UART byte I/O (RX IRQ is disabled during a transfer) --------
// Local software FIFO, drained from the small hardware FIFO so nothing is lost
// while we are busy writing to the SD/flash between packets (MMBasic _inbyte).
static unsigned char xm_fifo[FIFOSIZE];
static int xm_fifo_count;
static int xm_fifo_head;

static void xm_fifo_reset(void) {
    xm_fifo_count = 0;
    xm_fifo_head = 0;
}

// timeout in microseconds; 0 = just poll. Returns byte 0-255, or -1 on timeout.
static int _inbyte(int timeout) {
    int fifo_tail = (xm_fifo_head + xm_fifo_count) % FIFOSIZE;
    while (uart_is_readable(uart_default) && xm_fifo_count < FIFOSIZE) {
        xm_fifo[fifo_tail] = uart_getc(uart_default);
        fifo_tail = (fifo_tail + 1) % FIFOSIZE;
        xm_fifo_count++;
    }
    if (timeout == 0) {
        return xm_fifo_count > 0 ? xm_fifo_count : -1;
    }
    if (xm_fifo_count > 0) {
        int c = xm_fifo[xm_fifo_head];
        xm_fifo_head = (xm_fifo_head + 1) % FIFOSIZE;
        xm_fifo_count--;
        return c;
    }
    uint64_t timer = time_us_64() + timeout;
    while (time_us_64() < timer && !uart_is_readable(uart_default)) {
    }
    if (!uart_is_readable(uart_default)) {
        return -1;
    }
    xm_fifo_head = 0;
    xm_fifo_count = 0;
    return uart_getc(uart_default);
}

static void _outbyte(int c) {
    uint8_t b = (uint8_t)c;
    uart_write_blocking(uart_default, &b, 1);
}

static int xm_check(const unsigned char *buf, int sz) {
    unsigned char cks = 0;
    for (int i = 0; i < sz; ++i) {
        cks += buf[i];
    }
    return cks == buf[sz];
}

static void xm_flushinput(void) {
    while (_inbyte(((DLY_1S) * 3) >> 1) >= 0) {
    }
}

// --- file helpers via the MicroPython stream protocol ------------------------
// Read up to `want` bytes; returns the count actually read (0 at EOF).
static int file_get(mp_obj_t f, const mp_stream_p_t *sp, unsigned char *buf, int want) {
    int got = 0, err;
    while (got < want) {
        mp_uint_t r = sp->read(f, buf + got, want - got, &err);
        if (r == MP_STREAM_ERROR || r == 0) {
            break;
        }
        got += (int)r;
    }
    return got;
}

// Write `len` bytes; returns false on a stream error.
static bool file_put(mp_obj_t f, const mp_stream_p_t *sp, const unsigned char *buf, int len) {
    int done = 0, err;
    while (done < len) {
        mp_uint_t w = sp->write(f, buf + done, len - done, &err);
        if (w == MP_STREAM_ERROR) {
            return false;
        }
        done += (int)w;
    }
    return true;
}

// --- XMODEM receive (to file). Returns NULL on success, else an error string. -
static const char *xmodemReceive_file(mp_obj_t f, const mp_stream_p_t *sp) {
    unsigned char xbuff[X_BUF_SIZE];
    unsigned char pending[X_BLOCK_SIZE]; // last block, held back so its padding can be trimmed
    int have_pending = 0;
    unsigned char trychar = NAK;
    unsigned char packetno = 1;
    int i, c;
    int retry, retrans = MAXRETRANS;

    while (1) {
        for (retry = 0; retry < 32; ++retry) {
            if (trychar) {
                _outbyte(trychar);
            }
            if ((c = _inbyte((DLY_1S) << 1)) >= 0) {
                switch (c) {
                    case SOH:
                        goto start_recv;
                    case EOT:
                        xm_flushinput();
                        // Write the final block, trimming the sender's trailing
                        // padding (0x1A/0x00) so text/.py files aren't corrupted
                        // by pad bytes. Only the last block is trimmed; full
                        // blocks were already written verbatim.
                        if (have_pending) {
                            int n = X_BLOCK_SIZE;
                            while (n > 0 && (pending[n - 1] == PAD || pending[n - 1] == 0)) {
                                n--;
                            }
                            if (n > 0 && !file_put(f, sp, pending, n)) {
                                _outbyte(CAN);
                                _outbyte(CAN);
                                _outbyte(CAN);
                                return "File write error";
                            }
                            have_pending = 0;
                        }
                        _outbyte(ACK);
                        return NULL; // no more data
                    case CAN:
                        xm_flushinput();
                        _outbyte(ACK);
                        return "Cancelled by remote";
                    default:
                        break;
                }
            }
        }
        xm_flushinput();
        _outbyte(CAN);
        _outbyte(CAN);
        _outbyte(CAN);
        return "Remote did not respond";

    start_recv:
        trychar = 0;
        unsigned char *p = xbuff;
        *p++ = SOH;
        for (i = 0; i < (X_BLOCK_SIZE + 3); ++i) {
            if ((c = _inbyte(DLY_1S)) < 0) {
                goto reject;
            }
            *p++ = c;
        }
        if (xbuff[1] == (unsigned char)(~xbuff[2]) &&
            (xbuff[1] == packetno || xbuff[1] == (unsigned char)(packetno - 1)) &&
            xm_check(&xbuff[3], X_BLOCK_SIZE)) {
            if (xbuff[1] == packetno) {
                // Defer writes by one block: flush the previous block in full and
                // keep this one pending, so the LAST block's padding can be
                // trimmed at EOT (above).
                if (have_pending) {
                    if (!file_put(f, sp, pending, X_BLOCK_SIZE)) {
                        xm_flushinput();
                        _outbyte(CAN);
                        _outbyte(CAN);
                        _outbyte(CAN);
                        return "File write error";
                    }
                }
                memcpy(pending, &xbuff[3], X_BLOCK_SIZE);
                have_pending = 1;
                ++packetno;
                retrans = MAXRETRANS + 1;
            }
            if (--retrans <= 0) {
                xm_flushinput();
                _outbyte(CAN);
                _outbyte(CAN);
                _outbyte(CAN);
                return "Too many errors";
            }
            _outbyte(ACK);
            continue;
        }
    reject:
        xm_flushinput();
        _outbyte(NAK);
    }
}

// --- XMODEM transmit (from file). Returns NULL on success, else error string. -
static const char *xmodemTransmit_file(mp_obj_t f, const mp_stream_p_t *sp) {
    unsigned char buf[XPACKET_SIZE];
    int packet_num = 1;
    int c, use_crc = 0, retry;

    // Wait for the receiver to request start (NAK = checksum, 'C' = CRC).
    for (retry = 0; retry < 60; retry++) {
        c = _inbyte(1000000);
        if (c == NAK) {
            use_crc = 0;
            break;
        } else if (c == CRC16) {
            use_crc = 1;
            break;
        } else if (c == CAN) {
            return "Cancelled by remote";
        }
    }
    if (retry >= 60) {
        return "Receiver did not respond";
    }

    while (1) {
        memset(buf, 0, XPACKET_SIZE); // pad short final block with NULs
        int data_len = file_get(f, sp, buf, XPACKET_SIZE);
        if (data_len == 0) {
            break; // EOF
        }

        for (retry = 0; retry < XMAXRETRANS; retry++) {
            _outbyte(SOH);
            _outbyte(packet_num & 0xFF);
            _outbyte((~packet_num) & 0xFF);
            for (int i = 0; i < XPACKET_SIZE; i++) {
                _outbyte(buf[i]);
            }
            if (use_crc) {
                unsigned short crc = crc16_ccitt(buf, XPACKET_SIZE);
                _outbyte((crc >> 8) & 0xFF);
                _outbyte(crc & 0xFF);
            } else {
                unsigned char csum = 0;
                for (int i = 0; i < XPACKET_SIZE; i++) {
                    csum += buf[i];
                }
                _outbyte(csum);
            }

            c = _inbyte(PACKET_TIMEOUT);
            if (c == ACK) {
                break;
            } else if (c == CAN) {
                return "Cancelled by remote";
            }
            // NAK or timeout -> resend
        }
        if (retry >= XMAXRETRANS) {
            _outbyte(CAN);
            _outbyte(CAN);
            _outbyte(CAN);
            return "Too many errors";
        }
        packet_num = (packet_num + 1) & 0xFF;
    }

    // Send EOT, wait for ACK.
    for (retry = 0; retry < 10; retry++) {
        _outbyte(EOT);
        c = _inbyte(PACKET_TIMEOUT);
        if (c == ACK) {
            return NULL;
        }
    }
    return NULL; // EOT unacked, but the transfer is effectively complete
}

// --- module glue -------------------------------------------------------------
static uint xm_irq_num(void) {
    return uart_get_index(uart_default) ? UART1_IRQ : UART0_IRQ;
}

// Discard whatever is sitting in the hardware RX FIFO.
static void xm_drain_hw(void) {
    while (uart_is_readable(uart_default)) {
        (void)uart_getc(uart_default);
    }
}

// Run a transfer with the console RX IRQ disabled around it, so raw bytes reach
// the protocol; the IRQ is always restored, then any error is raised.
static mp_obj_t xm_run(mp_obj_t name_in, bool receive) {
    const char *mode = receive ? "wb" : "rb";
    mp_obj_t open_args[2] = { name_in, mp_obj_new_str(mode, 2) };
    mp_obj_t f = mp_vfs_open(2, open_args, (mp_map_t *)&mp_const_empty_map);
    const mp_stream_p_t *sp = mp_get_stream_raise(f, receive ? MP_STREAM_OP_WRITE : MP_STREAM_OP_READ);

    uint irq = xm_irq_num();
    irq_set_enabled(irq, false);
    xm_fifo_reset();
    xm_drain_hw();

    const char *err = receive ? xmodemReceive_file(f, sp) : xmodemTransmit_file(f, sp);

    xm_drain_hw();
    irq_set_enabled(irq, true);

    mp_stream_close(f);
    if (err != NULL) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), err);
    }
    return mp_const_none;
}

static mp_obj_t xmodem_recv(mp_obj_t name_in) {
    return xm_run(name_in, true);
}
static MP_DEFINE_CONST_FUN_OBJ_1(xmodem_recv_obj, xmodem_recv);

static mp_obj_t xmodem_send(mp_obj_t name_in) {
    return xm_run(name_in, false);
}
static MP_DEFINE_CONST_FUN_OBJ_1(xmodem_send_obj, xmodem_send);

static const mp_rom_map_elem_t xmodem_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_xmodem) },
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&xmodem_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&xmodem_send_obj) },
};
static MP_DEFINE_CONST_DICT(xmodem_module_globals, xmodem_module_globals_table);

const mp_obj_module_t xmodem_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&xmodem_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_xmodem, xmodem_module);

#endif // MICROPY_HW_ENABLE_UART_REPL
