from machine import UART, Pin
import time

uart = UART(0, 9600, tx=Pin(0), rx=Pin(1))    # most GPS modules default to 9600 baud
for _ in range(5):
    time.sleep(1)
    print(uart.read())
