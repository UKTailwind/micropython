import os
import time
import random

DELAY = 8                                # seconds per photo

def photo_list():
    try:
        files = os.listdir("/sd")
    except OSError:
        return []
    return sorted(f for f in files if f.lower().endswith((".jpg", ".jpeg")))

d = hdmi.fb()

while True:
    photos = photo_list()
    if not photos:
        d.fill(d.colour(BLACK))
        hdmi.text("no photos -- insert SD card", 40, 40, d.colour(GRAY))
        time.sleep(3)
        continue
    pick = random.choice(photos)
    try:
        draw_jpg("/sd/" + pick, dither=True)
    except OSError:
        continue                         # card pulled mid-read: just retry
    caption = pick.lower().replace(".jpeg", "").replace(".jpg", "")
    hdmi.text(caption, 8, hdmi.height() - 16, d.colour(LITEGRAY), d.colour(BLACK))
    time.sleep(DELAY)
