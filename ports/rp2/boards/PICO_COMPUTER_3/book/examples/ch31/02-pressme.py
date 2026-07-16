import time

button = Pin(1, Pin.IN, Pin.PULL_UP)

print("press the button (Ctrl-C to stop)")
was = 1
while True:
    now = button.value()
    if now == 0 and was == 1:          # just pressed (ch 25, in copper)
        beep()
        print("click!")
    was = now
    time.sleep_ms(5)
