import time

led = Pin(0, Pin.OUT)

for _ in range(20):
    led.toggle()
    time.sleep(0.25)
led.off()
