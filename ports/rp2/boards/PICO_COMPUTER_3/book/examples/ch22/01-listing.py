import pcgame

clock = pcgame.Clock(60)          # target: 60 beats per second
while True:
    # wait for the beat; dt = seconds elapsed
    dt = clock.tick()
    x += speed * dt               # speed is now PER SECOND
