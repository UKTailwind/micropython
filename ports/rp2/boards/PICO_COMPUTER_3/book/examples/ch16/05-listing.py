# copy a 100x80 patch of the screen 200px right
hdmi.blit(50, 50, 100, 80, 250, 50)

# shift a whole strip 4px left -- instant horizontal scrolling
hdmi.blit(4, 100, hdmi.width() - 4, 60, 0, 100)
