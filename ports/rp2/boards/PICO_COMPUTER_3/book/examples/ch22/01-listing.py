import pcgame

clock = pcgame.Clock(60)          # target: 60 beats per second
while True:
    dt = clock.tick()             # wait for the beat; dt = seconds elapsed
    x += speed * dt               # speed is now PER SECOND
