/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3  (MIT)
 */

// BMP support for the Pico Computer 3. bmp.save() writes the HDMI framebuffer to
// a 24-bit uncompressed .bmp (full colour, unlike MMBasic's 16-colour SAVE
// IMAGE, since we have full RGB565). BMP *loading* (draw_bmp) will be added
// here alongside, adapted from MMBasic's BmpDecoder.c.

#include <string.h>
#include "py/runtime.h"
#include "py/stream.h"

static void bmp_write_all(mp_obj_t f, const void *buf, size_t len) {
    const mp_stream_p_t *sp = mp_get_stream(f);
    const uint8_t *p = (const uint8_t *)buf;
    while (len) {
        int err;
        mp_uint_t n = sp->write(f, p, len, &err);
        if (n == MP_STREAM_ERROR) {
            mp_raise_OSError(err);
        }
        p += n;
        len -= n;
    }
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

// bmp.save(fbuf, w, h, is565, fileobj) -- write a 24-bit BMP of the framebuffer.
static mp_obj_t bmp_save(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t fbi;
    mp_get_buffer_raise(args[0], &fbi, MP_BUFFER_READ);
    int w = mp_obj_get_int(args[1]);
    int h = mp_obj_get_int(args[2]);
    bool is565 = mp_obj_is_true(args[3]);
    mp_obj_t f = args[4];

    int row_size = (w * 3 + 3) & ~3;         // rows padded to 4 bytes
    uint32_t img_size = (uint32_t)row_size * h;

    uint8_t fh[14] = { 'B', 'M' };
    put_u32(fh + 2, 54 + img_size);          // file size
    put_u32(fh + 10, 54);                    // pixel data offset (14 + 40)

    uint8_t ih[40] = { 0 };
    put_u32(ih + 0, 40);                     // BITMAPINFOHEADER size
    put_u32(ih + 4, (uint32_t)w);
    put_u32(ih + 8, (uint32_t)h);            // positive -> bottom-up
    ih[12] = 1;                              // planes
    ih[14] = 24;                             // bpp
    put_u32(ih + 20, img_size);
    put_u32(ih + 24, 2835);                  // 72 DPI (x)
    put_u32(ih + 28, 2835);                  // 72 DPI (y)

    bmp_write_all(f, fh, 14);
    bmp_write_all(f, ih, 40);

    uint8_t *rowbuf = m_malloc_maybe(row_size);
    if (rowbuf == NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("row buffer"));
    }
    memset(rowbuf, 0, row_size);             // padding bytes stay zero
    const uint16_t *fb16 = (const uint16_t *)fbi.buf;
    const uint8_t *fb8 = (const uint8_t *)fbi.buf;

    for (int y = h - 1; y >= 0; y--) {       // BMP rows are bottom-up
        uint8_t *o = rowbuf;
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            if (is565) {
                uint16_t p = fb16[y * w + x];
                uint8_t r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
                r = (r5 << 3) | (r5 >> 2);
                g = (g6 << 2) | (g6 >> 4);
                b = (b5 << 3) | (b5 >> 2);
            } else { // RGB332
                uint8_t p = fb8[y * w + x];
                uint8_t r3 = (p >> 5) & 7, g3 = (p >> 2) & 7, b2 = p & 3;
                r = (r3 << 5) | (r3 << 2) | (r3 >> 1);
                g = (g3 << 5) | (g3 << 2) | (g3 >> 1);
                b = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2;
            }
            *o++ = b; // BMP stores BGR
            *o++ = g;
            *o++ = r;
        }
        bmp_write_all(f, rowbuf, row_size);
    }
    m_free(rowbuf);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bmp_save_obj, 5, 5, bmp_save);

// --- BMP loading via the vendored decoder (bmp_decoder.c) -------------------

typedef struct {
    int width, height, bitsPerPixel;
    bool success;
    int linesProcessed;
} BMP_Result;
extern mp_obj_t g_bmp_file;
extern bool (*linecallback)(int *width, int *height, uint32_t *lineData, int *screenRow);
extern BMP_Result decodeBMP(bool topdown);

// Target for the decode line callback (one active load at a time).
static void *g_fb;
static int g_fbw, g_fbh, g_x0, g_y0;
static bool g_is565;

// Called by decodeBMP for each decoded line: RGB888-per-pixel -> framebuffer.
static bool bmp_fb_line(int *pw, int *ph, uint32_t *linedata, int *prow) {
    (void)ph;
    int w = *pw;
    int sy = g_y0 + *prow;
    if (sy < 0 || sy >= g_fbh) {
        return true; // off-screen row; keep going
    }
    uint16_t *fb16 = (uint16_t *)g_fb;
    uint8_t *fb8 = (uint8_t *)g_fb;
    for (int col = 0; col < w; col++) {
        int sx = g_x0 + col;
        if (sx < 0 || sx >= g_fbw) {
            continue;
        }
        uint32_t rgb = linedata[col];
        uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        if (g_is565) {
            fb16[sy * g_fbw + sx] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        } else {
            fb8[sy * g_fbw + sx] = (r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6);
        }
    }
    return true;
}

// bmp.load(fbuf, fb_w, fb_h, is565, fileobj, x, y) -> (img_w, img_h)
static mp_obj_t bmp_load(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t fbi;
    mp_get_buffer_raise(args[0], &fbi, MP_BUFFER_WRITE);
    g_fb = fbi.buf;
    g_fbw = mp_obj_get_int(args[1]);
    g_fbh = mp_obj_get_int(args[2]);
    g_is565 = mp_obj_is_true(args[3]);
    g_bmp_file = args[4];
    g_x0 = mp_obj_get_int(args[5]);
    g_y0 = mp_obj_get_int(args[6]);
    linecallback = bmp_fb_line;
    BMP_Result r = decodeBMP(false); // sequential read; screenRow is the display row
    linecallback = NULL;
    g_bmp_file = MP_OBJ_NULL;
    mp_obj_t items[2] = { mp_obj_new_int(r.width), mp_obj_new_int(r.height) };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bmp_load_obj, 7, 7, bmp_load);

static const mp_rom_map_elem_t bmp_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_bmp) },
    { MP_ROM_QSTR(MP_QSTR_save), MP_ROM_PTR(&bmp_save_obj) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&bmp_load_obj) },
};
static MP_DEFINE_CONST_DICT(bmp_module_globals, bmp_module_globals_table);

const mp_obj_module_t bmp_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bmp_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_bmp, bmp_module);
