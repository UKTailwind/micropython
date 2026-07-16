import time
import machine

print("I must be fed every 5 seconds. Hold SPACE to feed me.")
wdt = machine.WDT(timeout=5000)

while True:
    if keydown(1) == ord(" "):
        wdt.feed()
        print("fed.")
    time.sleep_ms(500)
