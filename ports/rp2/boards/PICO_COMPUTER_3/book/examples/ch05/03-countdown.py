import sys
import time

seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 5

for n in range(seconds, 0, -1):
    print(n, "...")
    beep(440, 100)
    time.sleep(1)
print("LIFT OFF!")
beep(880, 600)
