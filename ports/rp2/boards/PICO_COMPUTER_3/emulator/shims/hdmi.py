# PHASE-1 STAND-IN for the hdmi C module -- just enough state for the shell
# stack (sizes, mode, an in-memory framebuffer). Phase 2 replaces this file
# with the real hdmi.c drawing core compiled against the SDL backend; nothing
# should grow here beyond what pcshell/pcfm/pcconfig touch.

RGB640 = 0
RGB320 = 1
RGB512 = 2
RGB1024 = 3

# mode -> (width, height, bits-per-pixel)
_SIZES = {
    RGB640: (640, 480, 8),
    RGB320: (320, 240, 16),
    RGB512: (512, 300, 16),
    RGB1024: (1024, 600, 4),
}

_mode = RGB640
_fb = None


def init(mode=RGB640, clock=252):
    global _mode, _fb
    _mode = mode
    _fb = None


def deinit():
    global _fb
    _fb = None


def width():
    return _SIZES[_mode][0]


def height():
    return _SIZES[_mode][1]


def bpp():
    return _SIZES[_mode][2]


def framebuffer():
    global _fb
    if _fb is None:
        w, h, b = _SIZES[_mode]
        _fb = bytearray(w * h * b // 8)
    return memoryview(_fb)


def fb():
    return framebuffer()


def vsync():
    import time

    time.sleep_ms(16)


def fill(colour=0):
    if _fb is not None:
        for i in range(len(_fb)):
            _fb[i] = 0
