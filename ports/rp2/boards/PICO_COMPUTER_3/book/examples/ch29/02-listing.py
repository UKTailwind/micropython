import time

# tuple -> one big seconds count
t = time.mktime(gettime())
t += 90 * 24 * 60 * 60                  # add 90 days of seconds
future = time.localtime(t)              # seconds -> tuple again
print(f"{future[2]:02}/{future[1]:02}/{future[0]}")
