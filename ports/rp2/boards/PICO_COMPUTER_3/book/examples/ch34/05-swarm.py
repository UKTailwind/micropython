import time
import bench

W, H, N = 640, 480, 400

def make_stars():
    return [[i * 1.6 % W, (i * 7.3) % H, 40.0 + (i % 5) * 30] for i in range(N)]

# --- version 1: innocent
def naive(stars, dt):
    for s in stars:
        s[1] = s[1] + s[2] * dt
        if s[1] > H:
            s[1] = s[1] - H
        label = "stars: " + str(len(stars))      # a string, every frame,
    return label                                 # for no one

# --- version 2: this chapter applied
def tuned(stars, dt, _H=H):
    for s in stars:
        y = s[1] + s[2] * dt                     # locals, one calculation
        if y > _H:
            y -= _H
        s[1] = y
    return None                                  # the HUD can update ITSELF
                                                 # when the count CHANGES

stars = make_stars()
f1 = bench.it(lambda: naive(stars, 0.016), repeat=9)
stars = make_stars()
f2 = bench.it(lambda: tuned(stars, 0.016), repeat=9)
print(f"\ntuned is {f1 / f2:.2f}x quicker -- same stars, same physics")

budget = 16667
print(f"frame budget at 60 fps: {budget} us")
print(f"  naive swarm uses {f1 * 100 // budget}% of it")
print(f"  tuned swarm uses {f2 * 100 // budget}% of it")
