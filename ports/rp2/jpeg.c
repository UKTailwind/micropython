/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction... (MIT).
 */

// Baseline JPEG -> framebuffer, using the vendored picojpeg decoder (Rich
// Geldreich, public domain) and the MCU-row + binning logic from MMBasic's
// cmd_LoadJPGImage (FileIO.c) -- minus the dithering path. Decodes straight into
// an RGB565 or RGB332 framebuffer, with optional 1/2, 1/4, 1/8 downscaling
// (pixel binning / averaging).

#include <string.h>
#include "py/runtime.h"
#include "py/stream.h"
#include "picojpeg.h"
#include "dither.h"

// Nearest RGB121 palette index for a 4bpp (RGB121) framebuffer (defined in hdmi.c).
extern int hdmi_nearest_index(int r, int g, int b);

// picojpeg's work buffers are supplied by the caller via these globals (it
// declares them extern). Small and fixed, so plain static arrays -- no alloc.
static int16_t jb_coeff[64], jb_q0[64], jb_q1[64];
static uint8_t jb_r[256], jb_g[256], jb_b[256], jb_h2[256], jb_h3[256];
static uint8_t jb_in[PJPG_MAX_IN_BUF_SIZE];
int16_t *gCoeffBuf = jb_coeff;
uint8_t *gMCUBufR = jb_r, *gMCUBufG = jb_g, *gMCUBufB = jb_b;
int16_t *gQuant0 = jb_q0, *gQuant1 = jb_q1;
uint8_t *gHuffVal2 = jb_h2, *gHuffVal3 = jb_h3, *gInBuf = jb_in;

// The JPEG source: a Python file object read through the stream protocol.
static mp_obj_t g_jpeg_file = MP_OBJ_NULL;

static unsigned char jpeg_need_bytes(unsigned char *pBuf, unsigned char buf_size,
    unsigned char *pBytes_read, void *pData) {
    (void)pData;
    const mp_stream_p_t *sp = mp_get_stream(g_jpeg_file);
    unsigned int got = 0;
    while (got < buf_size) {
        int err;
        mp_uint_t r = sp->read(g_jpeg_file, pBuf + got, buf_size - got, &err);
        if (r == MP_STREAM_ERROR || r == 0) {
            break; // 0 = EOF, which picojpeg handles
        }
        got += r;
    }
    *pBytes_read = (unsigned char)got;
    return 0;
}

// Bin scale x scale source pixels (BGR) at output column ox into one RGB pixel.
static inline void jpeg_bin_pixel(const uint8_t *row, int row_stride, int line,
    int img_ly, int img_h, int img_w, int ox, int scale,
    uint8_t *r, uint8_t *g, uint8_t *b) {
    uint32_t sB = 0, sG = 0, sR = 0;
    int cnt = 0;
    for (int dy = 0; dy < scale && img_ly + dy < img_h; dy++) {
        const uint8_t *sp = row + (line + dy) * row_stride + (ox * scale) * 3;
        for (int dx = 0; dx < scale && ox * scale + dx < img_w; dx++) {
            sB += sp[0];
            sG += sp[1];
            sR += sp[2];
            sp += 3;
            cnt++;
        }
    }
    *b = (uint8_t)((sB + cnt / 2) / cnt);
    *g = (uint8_t)((sG + cnt / 2) / cnt);
    *r = (uint8_t)((sR + cnt / 2) / cnt);
}

// Blit one decoded MCU row (BGR, 3 bytes/pixel in `row`) to the framebuffer,
// binning scale x scale source pixels into each output pixel (scale 1 = 1:1).
// When `dith` is non-NULL the RGB121 output is error-diffusion dithered; `rgbrow`
// (out_w*3) and `idxrow` (out_w) are caller-provided scratch for that path.
static void jpeg_blit_row(const uint8_t *row, int row_stride, int image_y, int mcu_h,
    int img_w, int img_h, int x0, int y0, int scale,
    void *fbbuf, int fb_w, int fb_h, int bpp,
    dither_t *dith, uint8_t *rgbrow, uint8_t *idxrow) {
    uint16_t *fb16 = (uint16_t *)fbbuf;
    uint8_t *fb8 = (uint8_t *)fbbuf;
    int out_w = img_w / scale;
    for (int line = 0; line < mcu_h; line += scale) {
        int img_ly = image_y + line;
        if (img_ly >= img_h) {
            break;
        }
        int screen_y = y0 + img_ly / scale;

        if (dith != NULL) {
            // Dither the whole output row (even off-screen rows, to keep the error
            // state continuous), then write only the in-bounds pixels.
            for (int ox = 0; ox < out_w; ox++) {
                jpeg_bin_pixel(row, row_stride, line, img_ly, img_h, img_w, ox, scale,
                    &rgbrow[ox * 3 + 0], &rgbrow[ox * 3 + 1], &rgbrow[ox * 3 + 2]);
            }
            dither_row(dith, rgbrow, idxrow);
            if (screen_y >= 0 && screen_y < fb_h) {
                for (int ox = 0; ox < out_w; ox++) {
                    int screen_x = x0 + ox;
                    if (screen_x < 0 || screen_x >= fb_w) {
                        continue;
                    }
                    uint8_t v = idxrow[ox];
                    if (bpp == 4) {
                        uint8_t *pb = &fb8[(screen_y * fb_w + screen_x) >> 1];
                        *pb = (screen_x & 1) ? ((*pb & 0xf0) | v) : ((*pb & 0x0f) | (uint8_t)(v << 4));
                    } else { // bpp == 8, v is an RGB332 byte
                        fb8[screen_y * fb_w + screen_x] = v;
                    }
                }
            }
            continue;
        }

        if (screen_y < 0 || screen_y >= fb_h) {
            continue;
        }
        for (int ox = 0; ox < out_w; ox++) {
            int screen_x = x0 + ox;
            if (screen_x < 0 || screen_x >= fb_w) {
                continue;
            }
            uint8_t r, g, b;
            jpeg_bin_pixel(row, row_stride, line, img_ly, img_h, img_w, ox, scale, &r, &g, &b);
            if (bpp == 16) {
                fb16[screen_y * fb_w + screen_x] =
                    ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            } else if (bpp == 4) {
                uint8_t *pb = &fb8[(screen_y * fb_w + screen_x) >> 1];
                uint8_t v = (uint8_t)hdmi_nearest_index(r, g, b);
                *pb = (screen_x & 1) ? ((*pb & 0xf0) | v) : ((*pb & 0x0f) | (uint8_t)(v << 4));
            } else {
                fb8[screen_y * fb_w + screen_x] =
                    (r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6);
            }
        }
    }
}

// jpeg.render(fbuf, fb_w, fb_h, bpp, fileobj, x, y[, scale[, dither]]) -> (img_w, img_h)
//   dither = 0 none / 1 Floyd-Steinberg / 2 Atkinson (only used when bpp == 4)
static mp_obj_t jpeg_render(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t fbi;
    mp_get_buffer_raise(args[0], &fbi, MP_BUFFER_WRITE);
    int fb_w = mp_obj_get_int(args[1]);
    int fb_h = mp_obj_get_int(args[2]);
    int bpp = mp_obj_get_int(args[3]);
    g_jpeg_file = args[4];
    int x0 = mp_obj_get_int(args[5]);
    int y0 = mp_obj_get_int(args[6]);
    int scale = (n_args > 7) ? mp_obj_get_int(args[7]) : 1;
    int dither = (n_args > 8) ? mp_obj_get_int(args[8]) : DITHER_NONE;
    if (scale != 1 && scale != 2 && scale != 4 && scale != 8) {
        mp_raise_ValueError(MP_ERROR_TEXT("scale must be 1, 2, 4 or 8"));
    }
    // Dithering targets the RGB121 (4bpp) or RGB332 (8bpp) grids; RGB565 has no
    // dither path (and needs none).
    int dfmt = (bpp == 4) ? DITHER_FMT_RGB121 : DITHER_FMT_RGB332;
    if (bpp != 4 && bpp != 8) {
        dither = DITHER_NONE;
    }

    pjpeg_image_info_t info;
    uint8_t status = pjpeg_decode_init(&info, jpeg_need_bytes, NULL, 0);
    if (status) {
        g_jpeg_file = MP_OBJ_NULL;
        mp_raise_ValueError(status == PJPG_UNSUPPORTED_MODE
            ? MP_ERROR_TEXT("progressive JPEG not supported")
            : MP_ERROR_TEXT("not a valid JPEG"));
    }

    int row_stride = info.m_width * 3; // always store BGR (grayscale replicated)
    int mcu_h = info.m_MCUHeight;
    uint8_t *row = m_malloc_maybe((size_t)mcu_h * row_stride);
    if (row == NULL) {
        g_jpeg_file = MP_OBJ_NULL;
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("JPEG too wide"));
    }

    // Optional error-diffusion dithering (RGB1024/RGB640 only, per dfmt above).
    // Scratch is the output-row width; on OOM we silently fall back to the plain path.
    int out_w = info.m_width / scale;
    dither_t dith;
    uint8_t *rgbrow = NULL, *idxrow = NULL;
    bool dithering = dither_init(&dith, dither, dfmt, out_w);
    if (dithering) {
        rgbrow = m_malloc_maybe((size_t)out_w * 3);
        idxrow = m_malloc_maybe((size_t)out_w);
        if (rgbrow == NULL || idxrow == NULL) {
            dither_free(&dith);
            dithering = false;
        }
    }

    for (int mcu_y = 0; mcu_y < info.m_MCUSPerCol; mcu_y++) {
        int image_y = mcu_y * mcu_h;
        for (int mcu_x = 0; mcu_x < info.m_MCUSPerRow; mcu_x++) {
            status = pjpeg_decode_mcu();
            if (status) {
                goto finish; // PJPG_NO_MORE_BLOCKS or a decode error -> stop
            }
            int mcu_x_off = mcu_x * info.m_MCUWidth;
            for (int y = 0; y < mcu_h; y += 8) {
                int by_limit = MIN(8, info.m_height - (image_y + y));
                for (int x = 0; x < info.m_MCUWidth; x += 8) {
                    unsigned src_ofs = (x * 8U) + (y * 16U);
                    const uint8_t *pR = info.m_pMCUBufR + src_ofs;
                    const uint8_t *pG = info.m_pMCUBufG + src_ofs;
                    const uint8_t *pB = info.m_pMCUBufB + src_ofs;
                    int bx_limit = MIN(8, info.m_width - (mcu_x_off + x));
                    for (int by = 0; by < by_limit; by++) {
                        uint8_t *pDst = row + (y + by) * row_stride + (mcu_x_off + x) * 3;
                        if (info.m_comps == 1) {
                            for (int bx = 0; bx < bx_limit; bx++) {
                                pDst[0] = pDst[1] = pDst[2] = *pR++; // grayscale
                                pDst += 3;
                            }
                        } else {
                            for (int bx = 0; bx < bx_limit; bx++) {
                                pDst[0] = *pB++;
                                pDst[1] = *pG++;
                                pDst[2] = *pR++;
                                pDst += 3;
                            }
                            pG += (8 - bx_limit);
                            pB += (8 - bx_limit);
                        }
                        pR += (8 - bx_limit);
                    }
                }
            }
        }
        jpeg_blit_row(row, row_stride, image_y, mcu_h, info.m_width, info.m_height,
            x0, y0, scale, fbi.buf, fb_w, fb_h, bpp,
            dithering ? &dith : NULL, rgbrow, idxrow);
    }

finish:
    if (dithering) {
        dither_free(&dith);
        m_free(rgbrow);
        m_free(idxrow);
    }
    m_free(row);
    g_jpeg_file = MP_OBJ_NULL;
    mp_obj_t items[2] = { mp_obj_new_int(info.m_width), mp_obj_new_int(info.m_height) };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(jpeg_render_obj, 7, 9, jpeg_render);

static const mp_rom_map_elem_t jpeg_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_jpeg) },
    { MP_ROM_QSTR(MP_QSTR_render), MP_ROM_PTR(&jpeg_render_obj) },
};
static MP_DEFINE_CONST_DICT(jpeg_module_globals, jpeg_module_globals_table);

const mp_obj_module_t jpeg_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&jpeg_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_jpeg, jpeg_module);
