import math
import random
import pcmath
import ulab.numpy as np

def ask_float(prompt, fallback):
    try:
        return float(input(prompt).strip())
    except ValueError:
        return fallback

def pause():
    input("\n[Enter] for the menu ")
    cls()

def grapher():
    zoo = {"sin": math.sin,
           "cos": math.cos,
           "damped": lambda t: math.exp(-t / 4) * math.sin(3 * t),
           "squarish": lambda t: (math.sin(t) + math.sin(3 * t) / 3 +
                                  math.sin(5 * t) / 5)}
    print("the zoo:", ", ".join(zoo))
    name = input("which function? ").strip()
    fn = zoo.get(name, math.sin)
    a = ask_float("from (default 0): ", 0.0)
    b = ask_float("to (default 12.6): ", 12.6)
    plot(fn, (a, b))
    pause()

def stats():
    raw = input("numbers, comma-separated: ")
    try:
        data = [float(v) for v in raw.split(",")]
    except ValueError:
        print("that wasn't numbers -- try 3,1,4,1,5")
        return
    arr = np.array(data)
    print(f"n = {len(data)}   mean = {np.mean(arr):.3f}   "
          f"std = {np.std(arr):.3f}")
    print(f"min = {np.min(arr):.3f}   max = {np.max(arr):.3f}")
    plot(data, style="bar")
    pause()

def detective():
    rate = 128
    hz = random.randint(3, 40)
    sig = [math.sin(2 * math.pi * hz * i / rate) +
           random.randint(-80, 80) / 100 for i in range(rate)]
    plot(sig)
    input("\none second of signal, drowning in noise. [Enter] to analyse ")
    p = pcmath.power_spectrum(sig)
    peak = int(np.argmax(p[1:])) + 1        # skip slot 0 (the average)
    plot(p, style="bar")
    print(f"\nloudest slot: {peak} -> {peak} Hz   (the truth: {hz} Hz)")
    print("caught it!" if peak == hz else "noise won this round -- rerun me")
    pause()

def kinship():
    n = 40
    heights = [150 + random.randint(0, 400) / 10 for _ in range(n)]
    weights = [(h - 100) * 0.9 + random.randint(-120, 120) / 10
               for h in heights]
    r = pcmath.correl(heights, weights)
    plot(weights, x=heights, style="scatter")
    print(f"\nheight vs weight, n = {n}:   r = {r:.3f}")
    pause()

MENU = {"1": ("graph a function", grapher),
        "2": ("statistics of your numbers", stats),
        "3": ("the signal detective", detective),
        "4": ("correlation: height vs weight", kinship)}

cls()
while True:
    print("THE MATHS LAB")
    for key in sorted(MENU):
        print(f"  {key}. {MENU[key][0]}")
    choice = input("experiment (q quits): ").strip()
    if choice == "q":
        break
    if choice in MENU:
        cls()
        MENU[choice][1]()
