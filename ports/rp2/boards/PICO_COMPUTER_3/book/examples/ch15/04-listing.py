d = hdmi.fb()
d.fill(d.colour(0x87CEEB))                              # sky
d.fill_rect(0, 380, 640, 100, d.colour(MIDGREEN))       # ground
d.arc(500, 90, 0, 40, 0, 0, d.colour(YELLOW))           # sun (full disc)
d.line(0, 380, 240, 200, d.colour(GRAY), 4)             # mountain, left slope
d.line(240, 200, 480, 380, d.colour(GRAY), 4)           # right slope
d.flood(240, 300, d.colour(LITEGRAY))                   # snow... whole mountain
