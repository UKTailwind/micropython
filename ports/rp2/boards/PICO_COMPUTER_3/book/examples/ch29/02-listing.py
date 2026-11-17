import time

# tuple -> one big seconds count (mktime wants 8 values: the
# 6 from gettime, then weekday and day of year, which it ignores)
t = time.mktime(gettime() + (0, 0))
t += 90 * 24 * 60 * 60                  # add 90 days of seconds
future = time.localtime(t)              # seconds -> tuple again
print(f"{future[2]:02}/{future[1]:02}/{future[0]}")
