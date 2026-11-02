# iotest.py: machine's I/O on the CMM2 G2, through the loop-back wires GP0-GP8, GP1-GP9, GP3-GP11, GP4-GP12, GP5-GP13,
# GP6-GP14 and GP7-GP15 (tests/loopback). Prints PASS/FAIL lines and a summary.
#   RUN "A:/micropython.app", iotest.py
import time
from machine import Pin, PWM, ADC, UART, I2C, SPI, SoftSPI, time_pulse_us

PAIRS = [(0, 8), (1, 9), (3, 11), (4, 12), (5, 13), (6, 14), (7, 15)]
fails = []


def check(name, ok, detail=''):
    print('%s %s %s' % ('PASS' if ok else 'FAIL', name, detail))
    if not ok:
        fails.append(name)


def off(*gps):
    for g in gps:
        Pin(g, 0)                                   # (mode 0: the pin unconfigured)


# --- digital out to in, both ways, every pair ---
for a, b in PAIRS:
    for src, dst in ((a, b), (b, a)):
        o, i = Pin(src, Pin.OUT), Pin(dst, Pin.IN)
        got = []
        for v in (0, 1, 0, 1):
            o.value(v)
            time.sleep_us(20)
            got.append(i.value())
        check('GP%d->GP%d' % (src, dst), got == [0, 1, 0, 1], str(got))
    off(a, b)

# --- pulls, and open drain against a pull-up ---
o = Pin(3, Pin.IN)
i = Pin(11, Pin.IN, Pin.PULL_UP); time.sleep_us(50); up = i.value()
i = Pin(11, Pin.IN, Pin.PULL_DOWN); time.sleep_us(50); down = i.value()
check('pull-up/down', (up, down) == (1, 0), str((up, down)))
od = Pin(3, Pin.OPEN_DRAIN, value=1)
i = Pin(11, Pin.IN, Pin.PULL_UP)
od.value(0); time.sleep_us(50); lo = i.value()
od.value(1); time.sleep_us(50); hi = i.value()
check('open drain', (lo, hi) == (0, 1), str((lo, hi)))
check('Pin("GP3") is Pin(3)', Pin('GP3') is Pin(3))
off(3, 11)

# --- irq (polled every 2 ms) ---
edges = []
o, i = Pin(6, Pin.OUT, value=0), Pin(14, Pin.IN)
i.irq(lambda p: edges.append(p.value()), Pin.IRQ_RISING | Pin.IRQ_FALLING)
for k in range(10):
    o.toggle()
    time.sleep_ms(10)
i.irq(None)
check('irq edges', len(edges) == 10 and edges == [1, 0] * 5, str(edges))
off(6, 14)

# --- PWM on GP3-5, read on GP11-13 ---
for src, dst in ((3, 11), (4, 12), (5, 13)):
    p = PWM(Pin(src), freq=1000, duty_u16=16384)
    i = Pin(dst, Pin.IN)
    time.sleep_ms(5)
    hi = time_pulse_us(i, 1, 100000)
    lo = time_pulse_us(i, 0, 100000)
    check('PWM GP%d 1 kHz 25%%' % src, 230 < hi < 270 and 720 < lo < 780, 'high %d us low %d us' % (hi, lo))
    p.freq(5000)
    p.duty_u16(49152)
    time.sleep_ms(5)
    hi = time_pulse_us(i, 1, 100000)
    lo = time_pulse_us(i, 0, 100000)
    check('PWM GP%d 5 kHz 75%%' % src, 140 < hi < 160 and 40 < lo < 60, 'high %d us low %d us' % (hi, lo))
    p.deinit()
    off(dst)
off(3, 4, 5)

# --- UART(0): COM1 on GP0 (TX) and GP1 (RX), seen through GP8 and driven through GP9 ---
off(0, 1)
u = UART(0, 1200)
watch = Pin(8, Pin.IN)
u.write(b'\x00')                                    # start bit and 8 zeros low: 9 bits of 833 us
low = time_pulse_us(watch, 0, 100000)
check('UART TX 1200 baud', 7200 < low < 7800, 'low %d us' % low)
drive = Pin(9, Pin.OUT, value=1)
time.sleep_ms(5)
u.read()                                            # (anything before)
for byte in b'Hi':
    bits = [0] + [(byte >> k) & 1 for k in range(8)] + [1]
    t = time.ticks_us()
    for k, bit in enumerate(bits):
        drive.value(bit)
        while time.ticks_diff(time.ticks_us(), t) < (k + 1) * 833:
            pass
time.sleep_ms(20)
got = u.read()
check('UART RX 1200 baud', got == b'Hi', repr(got))
u.deinit()
off(8, 9)
u2 = UART(1, 9600)
check('UART(1) opens', u2.any() == 0, repr(u2))
u2.deinit()

# --- SoftSPI looped back (MOSI GP0 to MISO GP8) ---
s = SoftSPI(baudrate=100000, sck=Pin(5), mosi=Pin(0), miso=Pin(8))
out = bytes(range(0, 256, 17))
back = bytearray(len(out))
s.write_readinto(out, back)
check('SoftSPI loop-back', bytes(back) == out, back.hex())
off(0, 5, 8)

# --- I2C: no devices on Peter's board, so empty scans and a NACK ---
for bus in (0, 1):
    i2c = I2C(bus, freq=100000)
    found = i2c.scan()
    check('I2C(%d) scan' % bus, found == [], str(found))
    try:
        i2c.writeto(0x50, b'\x00')
        check('I2C(%d) NACK raises' % bus, False)
    except OSError as e:
        check('I2C(%d) NACK raises' % bus, True, repr(e))

# --- SPI: transfers without error (nothing on the bus) ---
for bus in (0, 1):
    spi = SPI(bus, baudrate=1000000)
    buf = bytearray(4)
    spi.write_readinto(b'\x55\xaa\x0f\xf0', buf)
    check('SPI(%d) transfer' % bus, True, repr(spi) + ' ' + buf.hex())
    spi.deinit()

# --- ADC on GP26-28 (nothing wired: a reading in range) ---
for ch in (0, 1, 2):
    a = ADC(ch)
    v, uv = a.read_u16(), a.read_uv()
    check('ADC(%d)' % ch, 0 <= v <= 65535 and 0 <= uv <= 3400000, '%r %d %d uV' % (a, v, uv))

# --- errors ---
for name, fn, exc in (('Pin(23)', lambda: Pin(23), ValueError), ('Pin(0, ANALOG)', lambda: Pin(0, Pin.ANALOG), ValueError),
                      ('PWM(Pin(6))', lambda: PWM(Pin(6), freq=100), ValueError), ('UART(2)', lambda: UART(2), ValueError)):
    try:
        fn()
        check(name + ' raises', False)
    except exc as e:
        check(name + ' raises', True, repr(e))

print('SUMMARY', 'all passed' if not fails else 'FAILED: ' + ', '.join(fails))
