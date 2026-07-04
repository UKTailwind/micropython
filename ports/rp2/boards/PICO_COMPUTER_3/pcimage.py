# Image loaders for the Pico Computer 3 -- decode to the HDMI framebuffer using
# the vendored C decoders (picojpeg, and later upng/BMP). Injected into the REPL
# as draw_jpg() by _boot.py.


def draw_jpg(path, x=0, y=0, scale=1):
    """Draw a JPEG onto the HDMI screen at (x, y), optionally downscaled by
    1/scale (scale = 1, 2, 4 or 8; averages for quality). Returns the source
    (width, height). The image is clipped to the screen."""
    import jpeg
    import hdmi

    with open(path, "rb") as f:
        return jpeg.render(
            hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.rgb565(), f, x, y, scale
        )


def draw_bmp(path, x=0, y=0):
    """Draw a BMP (1/4/8/16/24-bit, incl. RLE) onto the HDMI screen at (x, y).
    Returns the source (width, height). Clipped to the screen."""
    import bmp
    import hdmi

    with open(path, "rb") as f:
        return bmp.load(
            hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.rgb565(), f, x, y
        )


def draw_png(path, x=0, y=0, cutoff=20):
    """Draw a PNG (RGB8/RGBA8) onto the HDMI screen at (x, y). Pixels with alpha
    <= cutoff are left transparent. Returns the source (width, height)."""
    import png
    import hdmi

    with open(path, "rb") as f:
        data = f.read()  # upng references the buffer, so keep it alive here
    return png.render(
        hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.rgb565(), data, x, y, cutoff
    )


def save_image(path):
    """Save the whole HDMI screen to a 24-bit .bmp file."""
    import bmp
    import hdmi

    if not path.endswith(".bmp"):
        path += ".bmp"
    with open(path, "wb") as f:
        bmp.save(hdmi.framebuffer(), hdmi.width(), hdmi.height(), hdmi.rgb565(), f)
