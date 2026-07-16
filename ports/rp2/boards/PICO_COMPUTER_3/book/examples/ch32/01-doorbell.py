import time
import keyboard

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

presses = [0]                        # the flag the handler is allowed to touch

def ring(pin):
    presses[0] += 1                  # note it and LEAVE -- no real work here

button = Pin(1, Pin.IN, Pin.PULL_UP)
button.irq(ring, Pin.IRQ_FALLING)    # call ring() on each high -> low edge

print("busy doing something else entirely...")
announced = 0
try:
    while not held(keyboard.ESC):
        time.sleep_ms(200)           # deep in important work, clearly
        if presses[0] != announced:
            announced = presses[0]
            print(f"doorbell rang! ({announced} so far)")
            beep(880, 40)
finally:
    button.irq(None)                 # always disconnect your handlers
