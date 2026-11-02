# hdmitest.py: the Pico Computer 3's hdmi, keyboard and mouse modules on the CMM2.
#   RUN "A:/micropython.app", hdmitest.py
# Each mode shows its test card (8 colour bars), its name and a red box moving at the frame rate for 4 seconds; then
# RGB320 shows the mouse (a cross at its position) and the keys held until Escape is pressed.
import hdmi, framebuf, keyboard, mouse, time

MODES = [(hdmi.RGB640, 'RGB640', framebuf.GS8), (hdmi.RGB320_8, 'RGB320_8', framebuf.GS8),
         (hdmi.RGB320, 'RGB320', framebuf.RGB565), (hdmi.RGB512, 'RGB512', framebuf.RGB565),
         (hdmi.RGB640_4, 'RGB640_4', framebuf.GS4_HMSB), (hdmi.RGB1024, 'RGB1024', framebuf.GS4_HMSB)]
WHITE = {framebuf.GS8: 0xFF, framebuf.RGB565: 0xFFFF, framebuf.GS4_HMSB: 15}
RED = {framebuf.GS8: 0xE0, framebuf.RGB565: 0xF800, framebuf.GS4_HMSB: 8}


def show(mode, name, fmt, secs=4):
    hdmi.deinit()
    hdmi.init(mode)
    w, h = hdmi.width(), hdmi.height()
    hdmi.test()
    fb = framebuf.FrameBuffer(hdmi.framebuffer(), w, h, fmt)
    hdmi.text('%s %dx%d' % (name, w, h), 8, 8, WHITE[fmt], 0, 2)
    band, size = h - h // 5, h // 8
    x, dx, frames = 0, max(1, w // 160), 0
    end = time.ticks_add(time.ticks_ms(), secs * 1000)
    while time.ticks_diff(end, time.ticks_ms()) > 0:
        hdmi.vsync()
        fb.fill_rect(0, band, w, size, 0)
        fb.fill_rect(x, band, size, size, RED[fmt])
        x += dx
        if x < 0 or x + size > w:
            dx = -dx
            x += 2 * dx
        frames += 1
    print(name, w, h, frames / secs, 'frames a second')


def pointer():
    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    w, h = hdmi.width(), hdmi.height()
    fb = framebuf.FrameBuffer(hdmi.framebuffer(), w, h, framebuf.RGB565)
    while True:
        k = [keyboard.keydown(i) for i in range(8)]
        if 27 in k[1:7]:
            break
        x, y = mouse.query('X'), mouse.query('Y')
        hdmi.vsync()
        fb.fill(0)
        hdmi.text('Move the mouse, click, press keys', 4, 4, 0xFFFF, 0, 1)
        hdmi.text('Escape ends', 4, 18, 0xFFFF, 0, 1)
        hdmi.text('mouse %d,%d  buttons %d  wheel %d' % (x, y, mouse.query('B'), mouse.query('W')), 4, 40, 0x07E0, 0, 1)
        hdmi.text('keys held %d: %s  modifiers %d' % (k[0], ' '.join('%02X' % c for c in k[1:7] if c), k[7]),
                  4, 54, 0xFFE0, 0, 1)
        if mouse.query('D'):
            print('double click at', x, y)
        fb.hline(x - 8, y, 17, 0xF800)
        fb.vline(x, y - 8, 17, 0xF800)


for m in MODES:
    show(*m)
pointer()
hdmi.deinit()
print('hdmitest done')
