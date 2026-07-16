d = hdmi.fb()
msg = "GAME OVER"
w = len(msg) * 16 * 2                      # font 3, scale 2
hdmi.text(msg, (hdmi.width() - w) // 2, 200, d.colour(RED), -1, 2, 3)
