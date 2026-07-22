import time
import machine

servo = machine.PWM(Pin(2), freq=50)     # servos want 50 Hz

def angle(deg):
    # 0 deg = 0.5 ms pulse, 180 deg = 2.5 ms, out of a 20 ms frame
    us = 500 + deg * 2000 // 180
    servo.duty_u16(us * 65535 // 20000)

for _ in range(3):
    for a in range(0, 181, 5):
        angle(a)
        time.sleep_ms(20)
    for a in range(180, -1, -5):
        angle(a)
        time.sleep_ms(20)
# release (stops the hold jitter)
servo.duty_u16(0)
