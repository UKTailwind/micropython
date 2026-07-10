# Pico Computer 3 graphics helpers: the MMBasic named-colour palette (as 24-bit
# RGB888) plus a Display class that subclasses framebuf.FrameBuffer and converts
# RGB888 colours to the framebuffer's actual pixel format (RGB332 or RGB565).
#
# hdmi.fb() returns a Display, so:
#     fb = hdmi.fb()
#     fb.text("PICO COMPUTER 3", 8, 8, fb.colour(255, 0, 0))
#     fb.text("PICO COMPUTER 3", 8, 8, fb.colour(RED))   # RED injected by _boot

import framebuf

# MMBasic 16-colour palette (RGB888).
BLACK = 0x000000
BLUE = 0x0000FF
MYRTLE = 0x004000
COBALT = 0x0040FF
MIDGREEN = 0x008000
CERULEAN = 0x0080FF
GREEN = 0x00FF00
CYAN = 0x00FFFF
RED = 0xFF0000
MAGENTA = 0xFF00FF
RUST = 0xFF4000
FUCHSIA = 0xFF40FF
BROWN = 0xFF8000
LILAC = 0xFF80FF
YELLOW = 0xFFFF00
WHITE = 0xFFFFFF

# Extras.
GRAY = 0x808080
LITEGRAY = 0xD2D2D2
ORANGE = 0xFFA500
PINK = 0xFFA0AB
GOLD = 0xFFD700
SALMON = 0xFA8072


class Display(framebuf.FrameBuffer):
    """A framebuf.FrameBuffer whose colour() method converts RGB888 to the
    framebuffer's real pixel format, so drawing code can stay resolution-agnostic."""

    def __init__(self, buffer, width, height, fmt):
        super().__init__(buffer, width, height, fmt)
        self._is332 = fmt == framebuf.GS8
        self._is121 = fmt == framebuf.GS4_HMSB

    def colour(self, r, g=None, b=None):
        """Return an RGB888 colour packed for this display's format.
        colour(0xRRGGBB) or colour(r, g, b). In RGB1024 mode (GS4_HMSB / RGB121
        4-bit) this returns the nearest palette index (bit3=R, bits2:1=G, bit0=B)."""
        if g is None:  # r is a 24-bit RGB888 value
            b = r & 0xFF
            g = (r >> 8) & 0xFF
            r = (r >> 16) & 0xFF
        if self._is121:  # 4-bit: nearest of the 16 palette entries
            return (
                (0x08 if r >= 0x80 else 0)
                | (((g + 42) // 85) << 1)  # G to 0..3 (nearest of 0/85/170/255)
                | (1 if b >= 0x80 else 0)
            )
        if self._is332:  # RGB332
            return (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6)
        # RGB565
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

    color = colour  # US spelling alias
