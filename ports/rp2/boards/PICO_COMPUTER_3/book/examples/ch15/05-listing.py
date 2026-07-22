d = hdmi.fb()
d.text("Hello", 20, 20, d.colour(WHITE), font=3)           # 16x24
# 8x12, 4x size
d.text("BIG", 20, 60, d.colour(YELLOW), font=1, scale=4)
# the digits font
hdmi.text("12:04", 20, 130, d.colour(GREEN), -1, 1, 6)
