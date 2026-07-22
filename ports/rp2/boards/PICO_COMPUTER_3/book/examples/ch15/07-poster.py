d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

PAPER = d.colour(0x102040)
INK = d.colour(WHITE)
TRIM = d.colour(GOLD)
GLOW = d.colour(CYAN)

d.fill(PAPER)

# double frame
d.rbox(8, 8, W - 16, H - 16, 18, TRIM)
d.rbox(14, 14, W - 28, H - 28, 14, GLOW)

# headline, centred (font 3 = 16 wide, scale 2)
name = "ADA'S WORKSHOP"
w = len(name) * 16 * 2
hdmi.text(name, (W - w) // 2, 56, INK, -1, 2, 3)

# a ribbon of two bezier curves
d.bezier([(40, 200), (W // 4, 140), (3 * W // 4, 260), (W - 40,
         190)], GLOW)
d.bezier([(40, 210), (W // 4, 150), (3 * W // 4, 270), (W - 40,
         200)], GLOW)

# a row of medals: ring + hanger, spaced by loop arithmetic
for i in range(5):
    x = 80 + i * (W - 160) // 4
    d.arc(x, 330, 24, 30, 0, 0, TRIM)
    d.fill_rect(x - 2, 296, 4, 12, TRIM)

# small print (default font, transparent bg)
msg = "est. 2026  --  all robots welcome"
w = len(msg) * 8
hdmi.text(msg, (W - w) // 2, H - 60, d.colour(LITEGRAY))

save_image("poster.bmp")
