import time
import ds3231

# alarm: one minute from now (for the demo's sake)
h, m = gettime()[3:5]
m += 1
if m == 60:
    m, h = 0, (h + 1) % 24
ds3231.set_alarm(h, m)
print(f"alarm set for {h:02}:{m:02} -- doing nothing whatsoever...")

rang = [False]
def wake(pin):
    rang[0] = True

pin = ds3231.alarm_pin()
pin.irq(wake, Pin.IRQ_FALLING)       # the chip pulls the line low

try:
    while not rang[0]:
        time.sleep_ms(500)           # could be days; costs nothing
    print("WAKE UP")
    for _ in range(3):
        tone(880, 880, 150, wait=True)
        tone(1318, 1318, 250, wait=True)
finally:
    pin.irq(None)
    ds3231.clear_alarm()
    ds3231.alarm_off()
