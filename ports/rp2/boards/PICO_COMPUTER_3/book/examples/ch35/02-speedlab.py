import math
import micropython
import bench

N = 20000

def globals_loop():
    global gx
    gx = 0
    for i in range(N):
        gx = gx + 1

def locals_loop():
    x = 0
    for i in range(N):
        x = x + 1

def dotted():
    total = 0.0
    for i in range(N):
        # module.name looked up N times
        total += math.sin(0.5)

def hoisted():
    total = 0.0
    sin = math.sin                  # looked up ONCE
    for i in range(N):
        total += sin(0.5)

def churner():
    for i in range(N // 10):
        s = "score: " + str(i)      # a new string every lap

@micropython.native
def native_loop():
    x = 0
    for i in range(N):
        x = x + 1

print(f"--- {N} laps each ---")
g = bench.it(globals_loop)
l = bench.it(locals_loop)
bench.it(dotted)
bench.it(hoisted)
bench.it(churner)
n = bench.it(native_loop)
print(f"\nlocals beat globals by {g / l:.1f}x; native beats "
      f"plain "
      f"by {l / n:.1f}x")
