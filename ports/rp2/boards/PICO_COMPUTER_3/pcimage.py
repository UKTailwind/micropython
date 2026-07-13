# Image loaders for the Pico Computer 3 -- decode to the HDMI framebuffer using
# the vendored C decoders (picojpeg, upng, BMP). Injected into the REPL as
# draw_jpg()/draw_bmp()/draw_png()/save_image() by _boot_board.py. The decoders
# take hdmi.bpp() (4/8/16) and pack accordingly; in RGB1024 (4bpp) and RGB640 (8bpp)
# each pixel maps to the nearest palette / RGB332 colour, optionally error-diffusion
# dithered. Dithering has no effect in the RGB565 modes (RGB320/RGB512).

# Dithering modes (for draw_jpg / draw_bmp).
DITHER_NONE = 0
DITHER_FS = 1
DITHER_ATKINSON = 2


def _dither_mode(d):
    # dither=True -> Atkinson (the recommended one, esp. for RGB1024); False/None ->
    # off; an int (0/1/2) is passed straight through for explicit control.
    if d is True:
        return DITHER_ATKINSON
    if d is False or d is None:
        return DITHER_NONE
    return int(d)


def draw_jpg(path, x=0, y=0, scale=1, dither=False):
    """Draw a JPEG onto the HDMI screen at (x, y), optionally downscaled by
    1/scale (scale = 1, 2, 4 or 8; averages for quality). dither=True applies
    Atkinson dithering (recommended for RGB1024); dither=1 forces Floyd-Steinberg,
    2 Atkinson, 0/False none. Returns the source (width, height); clipped to the
    screen."""
    import jpeg
    import hdmi

    with open(path, "rb") as f:
        return jpeg.render(
            hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.bpp(),
            f, x, y, scale, _dither_mode(dither)
        )


def draw_bmp(path, x=0, y=0, dither=False):
    """Draw a BMP (1/4/8/16/24-bit, incl. RLE) onto the HDMI screen at (x, y).
    dither=True applies Atkinson dithering (recommended for RGB1024); dither=1
    forces Floyd-Steinberg, 2 Atkinson, 0/False none. Returns the source
    (width, height); clipped to the screen."""
    import bmp
    import hdmi

    with open(path, "rb") as f:
        return bmp.load(
            hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.bpp(),
            f, x, y, _dither_mode(dither)
        )


def draw_png(path, x=0, y=0, cutoff=20):
    """Draw a PNG (RGB8/RGBA8) onto the HDMI screen at (x, y). Pixels with alpha
    <= cutoff are left transparent. Returns the source (width, height)."""
    import png
    import hdmi

    with open(path, "rb") as f:
        data = f.read()  # upng references the buffer, so keep it alive here
    return png.render(
        hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.bpp(), data, x, y, cutoff
    )


def save_image(path):
    """Save the whole HDMI screen to a 24-bit .bmp file."""
    import bmp
    import hdmi

    if not path.endswith(".bmp"):
        path += ".bmp"
    with open(path, "wb") as f:
        bmp.save(hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.bpp(), f)


# --- Loading an image into memory (sprite sheets / off-screen graphics) -----


class Image:
    """An image decoded into memory in the current display format — a blit
    surface you can copy whole, or in pieces (e.g. sprite-sheet cells), with the
    blitter, without going through the framebuffer. Reload after a screen()
    mode change, because the pixel format differs per mode."""

    def __init__(self, buf, w, h):
        self.buf = buf
        self.w = w
        self.h = h

    @property
    def surface(self):
        """The (buffer, w, h) tuple, e.g. to pass to hdmi.blit()."""
        return (self.buf, self.w, self.h)

    def blit(self, x, y, sx=0, sy=0, w=None, h=None, dst=None, skip=-1):
        """Blit a region (default the whole image) to (x, y). `dst` is a target
        letter ("N"/"L"/"F") or None = the current write target; `skip` is a
        native colour that isn't copied (transparency)."""
        import hdmi

        hdmi.blit(sx, sy, self.w if w is None else w, self.h if h is None else h,
                  x, y, (self.buf, self.w, self.h), dst, skip)

    def cell(self, col, row, cw, ch, x, y, dst=None, skip=-1):
        """Blit sprite-sheet cell (col, row) of size cw x ch to (x, y)."""
        import hdmi

        hdmi.blit(col * cw, row * ch, cw, ch, x, y,
                  (self.buf, self.w, self.h), dst, skip)

    def sprites(self, cw, ch, count=None, transparent=None):
        """Cut the sheet into pcsprite Sprites (row-major) that SHARE this
        buffer — no per-sprite copy. `transparent` = the sprites' skip colour."""
        import pcsprite

        per_row = self.w // cw
        n = per_row * (self.h // ch) if count is None else count
        return [
            pcsprite.Sprite(self.buf, cw, ch, transparent, self.w, self.h,
                            (i % per_row) * cw, (i // per_row) * ch)
            for i in range(n)
        ]


def _jpeg_size(f):
    # Scan JPEG segments for the SOF marker and read (width, height).
    f.seek(2)  # skip SOI (FF D8)
    while True:
        b = f.read(1)
        if not b:
            raise ValueError("JPEG size not found")
        if b[0] != 0xFF:
            continue
        marker = f.read(1)[0]
        while marker == 0xFF:  # skip fill bytes
            marker = f.read(1)[0]
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):  # SOFn
            f.read(3)  # length(2) + precision(1)
            h = int.from_bytes(f.read(2), "big")
            w = int.from_bytes(f.read(2), "big")
            return w, h
        if marker in (0x01, 0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:  # no length
            continue
        f.seek(int.from_bytes(f.read(2), "big") - 2, 1)  # skip the segment


def load_image(path, transparent=None, dither=False, cutoff=20, scale=1):
    """Load an image file (.png/.jpg/.bmp) into memory as an Image — a blit
    surface in the current display format, for sprite sheets and off-screen
    graphics. `transparent` (a native colour) pre-fills the buffer, so a PNG's
    transparent areas become that colour, ready to use as a blit skip colour.
    `dither` (jpg/bmp) and `cutoff` (png alpha) match draw_*; `scale`
    downsamples a JPEG by 1/scale."""
    import hdmi

    ext = path.rsplit(".", 1)[-1].lower()
    with open(path, "rb") as f:
        if ext == "png":
            f.seek(16)
            d = f.read(8)
            w = int.from_bytes(d[0:4], "big")
            h = int.from_bytes(d[4:8], "big")
        elif ext in ("jpg", "jpeg"):
            w, h = _jpeg_size(f)
        else:  # bmp
            f.seek(18)
            d = f.read(8)
            w = int.from_bytes(d[0:4], "little")
            h = int.from_bytes(d[4:8], "little")
            if h >= 0x80000000:  # signed: negative = top-down
                h -= 0x100000000
            h = abs(h)
    ow = max(1, w // scale)
    oh = max(1, h // scale)
    bpp = hdmi.bpp()
    if bpp == 4 and (ow & 1):  # 4bpp packs 2 px/byte -> even width
        ow += 1
    buf = bytearray(ow * oh * bpp // 8)
    if transparent is not None:
        import framebuf

        fmt = framebuf.GS4_HMSB if bpp == 4 else (
            framebuf.GS8 if bpp == 8 else framebuf.RGB565)
        framebuf.FrameBuffer(buf, ow, oh, fmt).fill(transparent)
    if ext in ("jpg", "jpeg"):
        import jpeg

        with open(path, "rb") as f:
            jpeg.render(buf, ow, oh, bpp, f, 0, 0, scale, _dither_mode(dither))
    elif ext == "png":
        import png

        with open(path, "rb") as f:
            data = f.read()
        png.render(buf, ow, oh, bpp, data, 0, 0, cutoff)
    else:
        import bmp

        with open(path, "rb") as f:
            bmp.load(buf, ow, oh, bpp, f, 0, 0, _dither_mode(dither))
    return Image(buf, ow, oh)
