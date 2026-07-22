# where.py -- read a GPS receiver on GP0/GP1 and report position.
from machine import UART, Pin
from micropyGPS import MicropyGPS
import time

uart = UART(0, 9600, tx=Pin(0), rx=Pin(1))
# 'dd' = plain decimal degrees
gps = MicropyGPS(location_formatting='dd')

print("waiting for a fix -- a clear view of the sky helps "
      "(Ctrl-C stops)")
while True:
    while uart.any():
        # feed each character in
        gps.update(chr(uart.read(1)[0]))
    if gps.satellites_in_use:
        # e.g. [51.5074, 'N']
        lat = gps.latitude
        # e.g. [0.1278, 'W']
        lon = gps.longitude
        print(f"\r{lat[0]}{lat[1]}  {lon[0]}{lon[1]}   "
              f"({gps.satellites_in_use} sats)   ", end="")
    time.sleep_ms(200)
