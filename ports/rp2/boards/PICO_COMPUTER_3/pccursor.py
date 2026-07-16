# Mouse pointer overlay for the Pico Computer 3 -- MMBasic's GUI CURSOR.
#
# A save-under sprite pointer: the pixels beneath it are saved before it is
# drawn and restored when it moves, so it floats over whatever is on screen
# without disturbing it. The two shapes are MMBasic's built-in cursors
# (PicoMite Pointer.c): ARROW (13x19, hot point at the tip) and CROSS
# (15x15, hot point at the centre). All pixel work is C (hdmi.blit); Python
# only bookkeeps one rectangle.
#
#     import pccursor
#     pccursor.on()                 # arrow pointer, tracks the USB mouse
#     while True:
#         pccursor.refresh()        # erase + repaint if the mouse moved
#         ...
#
# pcgui does this automatically: GUI.start() turns the pointer on when a
# mouse is present, GUI.poll() refreshes it, and every control draw path
# calls erase() first so the saved patch never captures stale pixels.

import hdmi
import mouse

import pcgfx

ARROW = 0
CROSS = 1

# MMBasic's built-in cursor sprites (PicoMite Pointer.c, verbatim): one int
# per row, bit n set = an opaque pixel in column n (drawn in the pointer
# colour); clear bits are transparent. (bits, w, h, hot_x, hot_y) -- the hot
# point is the click-meaningful pixel: when the pointer "is at" (x, y) that
# pixel lands on (x, y) and the sprite is drawn at (x - hot_x, y - hot_y).
_SHAPES = (
    # ARROW: 13x19, hot point at the tip (0, 0)
    ((0x0001, 0x0003, 0x0005, 0x0009, 0x0011, 0x0021, 0x0041, 0x0081,
      0x0101, 0x0201, 0x0401, 0x0801, 0x1F81, 0x0091, 0x0099, 0x0125,
      0x0123, 0x0240, 0x03C0), 13, 19, 0, 0),
    # CROSS: 15x15, hot point at the centre (7, 7)
    ((0x0080,) * 7 + (0x7FFF,) + (0x0080,) * 7, 15, 15, 7, 7),
)

_enabled = False
_hidden = False
_painted = False   # pixels currently on screen
_shape = ARROW
_rgb = pcgfx.WHITE
_gen = -1          # hdmi.gen() the buffers were built for; a mode change rebuilds
_x = -1            # hot-point position (-1 = never placed -> centre)
_y = -1
_px = _py = 0      # hot-point position as last painted
_ex = _ey = 0      # sprite top-left as last painted (the erase rectangle)
_spr = None        # sprite pixels, mode-native, _bw x _h
_save = None       # pixels-under save buffer, _bw x _h
_bw = _h = 0       # buffer geometry (_bw = width padded even for the 4bpp mode)
_hx = _hy = 0      # hot-point offset within the sprite
_skip = 0          # native colour treated as transparent in _spr


def _build():
    """(Re)build the sprite + save buffers for the current mode and colour."""
    global _spr, _save, _bw, _h, _hx, _hy, _skip, _gen, _painted
    bits, w, h, hx, hy = _SHAPES[_shape]
    bw = (w + 1) & ~1  # even width: the 4bpp mode packs two pixels per byte
    bpp = hdmi.bpp()
    c = hdmi.fb().colour(_rgb)
    skip = 1 if c == 0 else 0  # any native value the sprite doesn't use
    if bpp == 4:
        spr = bytearray((bw * h) >> 1)
        if skip:
            v = (skip << 4) | skip
            for i in range(len(spr)):
                spr[i] = v
        for yy in range(h):
            row = bits[yy]
            for xx in range(w):
                if row & (1 << xx):
                    i = yy * (bw >> 1) + (xx >> 1)
                    if xx & 1:
                        spr[i] = (spr[i] & 0x0F) | ((c & 0x0F) << 4)
                    else:
                        spr[i] = (spr[i] & 0xF0) | (c & 0x0F)
    elif bpp == 8:
        spr = bytearray(bw * h)
        if skip:
            for i in range(len(spr)):
                spr[i] = skip
        for yy in range(h):
            row = bits[yy]
            for xx in range(w):
                if row & (1 << xx):
                    spr[yy * bw + xx] = c
    else:  # 16bpp, little-endian
        spr = bytearray(bw * h * 2)
        if skip:
            for i in range(0, len(spr), 2):
                spr[i] = skip & 0xFF
                spr[i + 1] = skip >> 8
        for yy in range(h):
            row = bits[yy]
            for xx in range(w):
                if row & (1 << xx):
                    i = (yy * bw + xx) * 2
                    spr[i] = c & 0xFF
                    spr[i + 1] = (c >> 8) & 0xFF
    _spr = spr
    _save = bytearray(len(spr))
    _bw = bw
    _h = h
    _hx = hx
    _hy = hy
    _skip = skip
    _gen = hdmi.gen()
    _painted = False


def erase():
    """Restore the pixels under the pointer (no-op when it isn't painted).
    Call before drawing something that may overlap it; the next refresh()
    repaints. pcgui's draw paths do this automatically."""
    global _painted
    if not _painted:
        return
    if hdmi.gen() == _gen:  # after a mode change the saved patch is void
        hdmi.blit(0, 0, _bw, _h, _ex, _ey, (_save, _bw, _h), "N")
    _painted = False


def _paint():
    global _painted, _px, _py, _ex, _ey
    sx = _x - _hx
    sy = _y - _hy
    # Save the pixels underneath, then draw the sprite over them. Both blits
    # pass the raw rectangle: hdmi.blit clips source and destination in step,
    # so a pointer partly off any screen edge saves/draws/erases consistently.
    hdmi.blit(sx, sy, _bw, _h, 0, 0, "N", (_save, _bw, _h))
    hdmi.blit(0, 0, _bw, _h, sx, sy, (_spr, _bw, _h), "N", _skip)
    _painted = True
    _px = _x
    _py = _y
    _ex = sx
    _ey = sy


def refresh():
    """Track the mouse and repaint the pointer if it moved. Call often (a
    GUI/game loop is ideal); GUI.poll() calls it for you."""
    global _x, _y, _painted
    if not _enabled:
        return
    if hdmi.gen() != _gen:
        _painted = False  # the old screen (and the saved patch) are gone
        _build()
    if mouse.query("PRESENT"):
        _x = mouse.query("X")
        _y = mouse.query("Y")
    if _hidden:
        erase()
        return
    if _painted and _x == _px and _y == _py:
        return
    erase()
    _paint()


def on(shape=ARROW, colour=None, x=None, y=None):
    """Show the pointer: `shape` ARROW or CROSS, `colour` 24-bit RGB (default
    white). It appears at the mouse position and follows the mouse on every
    refresh(); without a mouse it sits at (x, y) (default screen centre)
    until move()d. Call again any time to change shape or colour."""
    global _enabled, _hidden, _shape, _rgb, _x, _y
    if not 0 <= shape < len(_SHAPES):
        raise ValueError("shape must be ARROW or CROSS")
    erase()
    _shape = shape
    _rgb = pcgfx.WHITE if colour is None else colour
    _build()
    if x is not None:
        _x = x
    if y is not None:
        _y = y
    if _x < 0:  # never placed and no mouse to place it: start centred
        _x = hdmi.width() // 2
        _y = hdmi.height() // 2
    _enabled = True
    _hidden = False
    refresh()


def off():
    """Remove the pointer and restore the screen beneath it."""
    global _enabled
    erase()
    _enabled = False


def hide():
    """Take the pointer off the screen but keep it enabled (see show())."""
    global _hidden
    _hidden = True
    erase()


def show():
    """Put a hide()-den pointer back on the screen."""
    global _hidden
    _hidden = False
    refresh()


def move(x, y):
    """Put the hot point at (x, y): steering for programs without a mouse
    (when a mouse is present it overrides this on the next refresh())."""
    global _x, _y
    _x = x
    _y = y
    if _enabled:
        refresh()


def pos():
    """The pointer's hot-point position as (x, y)."""
    return _x, _y


def active():
    """True while the pointer is enabled (between on() and off())."""
    return _enabled
