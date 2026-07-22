import time
import keyboard

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

# the flag the handler is allowed to touch
presses = [0]

def ring(pin):
    # note it and LEAVE -- no real work here
    presses[0] += 1

button = Pin(1, Pin.IN, Pin.PULL_UP)
# call ring() on each high -> low edge
button.irq(ring, Pin.IRQ_FALLING)

print("busy doing something else entirely...")
announced = 0
try:
    while not held(keyboard.ESC):
        # deep in important work, clearly
        time.sleep_ms(200)
        if presses[0] != announced:
            announced = presses[0]
            print(f"doorbell rang! ({announced} so far)")
            beep(880, 40)
finally:
    # always disconnect your handlers
    button.irq(None)
