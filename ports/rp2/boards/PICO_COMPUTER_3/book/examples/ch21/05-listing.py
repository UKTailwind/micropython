import time
t0 = time.ticks_ms()
# ... something happens ...
elapsed = time.ticks_diff(time.ticks_ms(), t0)
