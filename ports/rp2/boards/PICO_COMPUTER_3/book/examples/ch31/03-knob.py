import time
import machine

knob = machine.ADC(Pin(40))

for _ in range(200):
    raw = knob.read_u16()              # 0 .. 65535
    percent = raw * 100 // 65535
    print(f"\r{'#' * (percent // 5):20} {percent:3}%", end="")
    time.sleep_ms(50)
print()
