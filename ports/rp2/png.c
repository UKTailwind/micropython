/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)  Copyright (c) 2026 Pico Computer 3
 */

// PNG -> framebuffer, using the vendored upng decoder (Lode Vandevenne / Sean
// Middleditch, public domain, from MMBasic third_party_mod). The whole file is
// read in Python and handed to upng as bytes; upng decodes to RGB8/RGBA8, and we
// blit to RGB565/RGB332 with alpha-cutoff transparency (like MMBasic's LOAD PNG).

#include "py/runtime.h"
#include "upng.h"

// Nearest RGB121 palette index for a 4bpp (RGB121) framebuffer (defined in hdmi.c).
extern int hdmi_nearest_index(int r, int g, int b);

// png.render(fbuf, fb_w, fb_h, bpp, data, x, y[, cutoff]) -> (img_w, img_h)
//   bpp = 16 (RGB565), 8 (RGB332) or 4 (RGB121, packed 2 px/byte)
// `data` = the whole PNG file (bytes). Pixels with alpha <= cutoff are skipped
// (leaving the framebuffer contents), giving simple transparency.
static mp_obj_t png_render(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t fbi, di;
    mp_get_buffer_raise(args[0], &fbi, MP_BUFFER_WRITE);
    int fb_w = mp_obj_get_int(args[1]);
    int fb_h = mp_obj_get_int(args[2]);
    int bpp = mp_obj_get_int(args[3]);
    mp_get_buffer_raise(args[4], &di, MP_BUFFER_READ);
    int x0 = mp_obj_get_int(args[5]);
    int y0 = mp_obj_get_int(args[6]);
    int cutoff = (n_args > 7) ? mp_obj_get_int(args[7]) : 20;

    upng_t *upng = upng_new_from_bytes((const unsigned char *)di.buf, di.len);
    if (upng == NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("PNG"));
    }
    upng_decode(upng);
    if (upng_get_error(upng) != UPNG_EOK) {
        upng_free(upng);
        mp_raise_ValueError(MP_ERROR_TEXT("bad or unsupported PNG"));
    }
    int w = upng_get_width(upng);
    int h = upng_get_height(upng);
    upng_format fmt = upng_get_format(upng);
    int comps = (fmt == UPNG_RGBA8) ? 4 : (fmt == UPNG_RGB8) ? 3 : 0;
    if (comps == 0) {
        upng_free(upng);
        mp_raise_ValueError(MP_ERROR_TEXT("PNG must be RGB8 or RGBA8"));
    }

    const uint8_t *px = (const uint8_t *)upng_get_buffer(upng);
    uint16_t *fb16 = (uint16_t *)fbi.buf;
    uint8_t *fb8 = (uint8_t *)fbi.buf;
    for (int iy = 0; iy < h; iy++) {
        int sy = y0 + iy;
        for (int ix = 0; ix < w; ix++) {
            uint8_t r = px[0], g = px[1], b = px[2];
            uint8_t a = (comps == 4) ? px[3] : 255;
            px += comps;
            if (sy < 0 || sy >= fb_h) {
                continue;
            }
            int sx = x0 + ix;
            if (sx < 0 || sx >= fb_w || a <= cutoff) {
                continue;
            }
            if (bpp == 16) {
                fb16[sy * fb_w + sx] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            } else if (bpp == 4) {
                uint8_t *pb = &fb8[(sy * fb_w + sx) >> 1];
                uint8_t v = (uint8_t)hdmi_nearest_index(r, g, b);
                *pb = (sx & 1) ? ((*pb & 0xf0) | v) : ((*pb & 0x0f) | (uint8_t)(v << 4));
            } else {
                fb8[sy * fb_w + sx] = (r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6);
            }
        }
    }
    upng_free(upng);
    mp_obj_t items[2] = { mp_obj_new_int(w), mp_obj_new_int(h) };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(png_render_obj, 7, 8, png_render);

static const mp_rom_map_elem_t png_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_png) },
    { MP_ROM_QSTR(MP_QSTR_render), MP_ROM_PTR(&png_render_obj) },
};
static MP_DEFINE_CONST_DICT(png_module_globals, png_module_globals_table);

const mp_obj_module_t png_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&png_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_png, png_module);
