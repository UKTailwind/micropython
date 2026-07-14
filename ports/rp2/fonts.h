// MMBasic HDMI font set for the Pico Computer 3.
//
// Every font is a fixed-width bitmap in MMBasic's format: a 4-byte header
// { width, height, first-char, char-count } followed by the glyphs. Each glyph
// is width*height bits, packed as one continuous MSB-first bitstream (NOT
// byte-aligned per row), so pixel (x,y) of a glyph is bit N = y*width + x:
//     on = (glyph[N >> 3] >> (7 - (N & 7))) & 1;
// (width*height is a multiple of 8 for every font here, which is what makes
// that simple form exact.) This matches PicoMite's Draw.c renderer, so the
// fonts render identically.
//
// The int / uint32_t fonts (Inconsola, TinyFont) are reinterpreted as bytes;
// on the little-endian RP2350 that yields the same byte order MMBasic uses.
//
// Only #included by hdmi.c (which is gated on MICROPY_HW_ENABLE_HDMI), so the
// ~130 KB of glyph data never reaches boards without the HDMI display.
#ifndef MICROPY_INCLUDED_RP2_FONTS_H
#define MICROPY_INCLUDED_RP2_FONTS_H

#include <stdint.h>

#include "console_font.h"            // font 1: 8x12  (const unsigned char font1[])
#include "fonts/Misc_12x20_LE.h"     // font 2: 12x20
#include "fonts/Hom_16x24_LE.h"      // font 3: 16x24
#include "fonts/Fnt_10x16.h"         // font 4: 10x16
#include "fonts/Inconsola.h"         // font 5: 24x32 (const int)
#include "fonts/ArialNumFontPlus.h"  // font 6: 32x50, digits '0'..':' only
#include "fonts/Font_8x6.h"          // font 7: 6x8   (F_6x8_LE)
#include "fonts/smallfont.h"         // font 8: 4x6   (TinyFont, const uint32_t)
#include "fonts/font-8x10.h"         // font 9: 8x10  (font8x10)

// 1-based MMBasic font numbers map to these entries (index = font number - 1).
static const uint8_t *const hdmi_fonts[] = {
    (const uint8_t *)font1,             // 1
    (const uint8_t *)Misc_12x20_LE,     // 2
    (const uint8_t *)Hom_16x24_LE,      // 3
    (const uint8_t *)Fnt_10x16,         // 4
    (const uint8_t *)Inconsola,         // 5
    (const uint8_t *)ArialNumFontPlus,  // 6
    (const uint8_t *)F_6x8_LE,          // 7
    (const uint8_t *)TinyFont,          // 8
    (const uint8_t *)font8x10,          // 9
};
#define HDMI_NFONTS ((int)(sizeof(hdmi_fonts) / sizeof(hdmi_fonts[0])))

#endif // MICROPY_INCLUDED_RP2_FONTS_H
