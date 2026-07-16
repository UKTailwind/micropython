import time

black = hdmi.fb().colour(BLACK)
for _ in range(200):
    hdmi.scroll(2, black)
    time.sleep(0.02)
