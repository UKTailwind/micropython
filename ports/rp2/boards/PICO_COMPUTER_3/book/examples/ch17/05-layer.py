import time

screen(hdmi.RGB320)
time.sleep(3)                      # let the monitor lock the new mode

d = hdmi.fb()                      # the display (320x240 now)
d.fill(d.colour(0x104060))         # "rich scenery": a sea...
for i in range(8):                 # ...with waves, drawn ONCE
    d.line(0, 120 + i * 14, 319, 126 + i * 14, d.colour(CERULEAN), 2)

hdmi.close("L")                    # start clean (rerun-proof)
hdmi.layer()                       # acetate on. black = see-through
hdmi.write("L")
s = hdmi.fb()                      # a Display over the LAYER

console("none")

try:
    s.text("SCORE 100", 8, 8, s.colour(YELLOW))
    for x in range(0, 280, 4):                       # a sprite crosses...
        s.fill_rect(x, 150, 16, 16, s.colour(RED))
        hdmi.vsync()
        s.fill_rect(x, 150, 16, 16, 0)               # erase = draw black
    s.fill(0)                      # wipe the acetate -- scenery unharmed
finally:
    hdmi.write("N")
    console()
