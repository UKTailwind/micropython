# imgtest.py: the Pico Computer 3's image loaders (jpeg, png, bmp) on the CMM2.
#   RUN "A:/micropython.app", /media/imgtest.py
# The same 320x240 card from each format: RGB320 (65536 colours) shows the JPEG; RGB640 (256 colours) shows the PNG
# at the top left and the BMP at the bottom right; RGB640_4 (16 colours) shows the JPEG plain and dithered. 4 s each.
import hdmi, jpeg, png, bmp, time

D = '/media/'


def mode(m):
    hdmi.deinit()
    hdmi.init(m)
    return hdmi.framebuffer(), hdmi.width(), hdmi.height()


def timed(name, fn):
    t = time.ticks_ms()
    r = fn()
    print(name, r, time.ticks_diff(time.ticks_ms(), t), 'ms')


fb, w, h = mode(hdmi.RGB320)
with open(D + 'card.jpg', 'rb') as f:
    timed('jpeg RGB320', lambda: jpeg.render(fb, w, h, 16, f, 0, 0))
time.sleep(4)

fb, w, h = mode(hdmi.RGB640)
with open(D + 'card.png', 'rb') as f:
    data = f.read()
timed('png RGB640', lambda: png.render(fb, w, h, 8, data, 0, 0))
with open(D + 'card.bmp', 'rb') as f:
    timed('bmp RGB640', lambda: bmp.load(fb, w, h, 8, f, 320, 240))
time.sleep(4)

fb, w, h = mode(hdmi.RGB640_4)
with open(D + 'card.jpg', 'rb') as f:
    timed('jpeg RGB640_4', lambda: jpeg.render(fb, w, h, 4, f, 0, 0))
with open(D + 'card.jpg', 'rb') as f:
    timed('jpeg RGB640_4 dithered', lambda: jpeg.render(fb, w, h, 4, f, 320, 240, 1, 1))
time.sleep(4)
hdmi.deinit()
print('imgtest done')
