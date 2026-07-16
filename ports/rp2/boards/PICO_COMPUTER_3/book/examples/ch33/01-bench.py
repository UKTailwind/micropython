# bench.py -- measure before you optimise.
#   import bench
#   bench.it(my_function)          # prints and returns best-of microseconds
import time

def it(fn, repeat=5):
    """Time fn() five times; report the BEST run in microseconds."""
    best = None
    for _ in range(repeat):
        t0 = time.ticks_us()
        fn()
        us = time.ticks_diff(time.ticks_us(), t0)
        if best is None or us < best:
            best = us
    print(f"{fn.__name__}: {best} us  (best of {repeat})")
    return best

if __name__ == "__main__":
    def nothing():
        pass
    it(nothing)                    # the cost of measuring itself
