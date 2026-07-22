from machine import UART, Pin
import time

# most GPS modules default to 9600 baud
uart = UART(0, 9600, tx=Pin(0), rx=Pin(1))
for _ in range(5):
    time.sleep(1)
    print(uart.read())
