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
