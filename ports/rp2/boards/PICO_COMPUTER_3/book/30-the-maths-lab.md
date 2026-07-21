# Chapter 30 — Numbers at speed: the maths lab

Under the games and gadgets, this machine is a serious scientific
instrument: it carries **ulab**, a working dialect of NumPy — the
array library that runs modern science — plus `pcmath`'s statistics
and signal tools, and the `plot()` you met in chapter 8. This chapter
turns them into a laboratory. And a quiet promise as you start: the
`np.` idiom you learn here is *the same idiom* used on
million-core supercomputers — of everything in this book, this
chapter's skill travels furthest.

## `plot()`, properly this time

Chapter 8 borrowed it; here is the whole instrument. It draws
**data** or **functions**, autoscaled with axes:

```python
plot([3, 1, 4, 1, 5, 9, 2, 6])          # a sequence of values
```

```python
import math
plot(math.sin, (0, 12.6))               # a FUNCTION over a range
```

That second form is a graphing calculator in one line — any function
of one number, including your own `def`s. The refinements:

- **Real x-values**: `plot(temps, x=hours)` plots pairs, so the axes
  mean something (unequal spacing welcome).
- **Several series**: `plot([series_a, series_b])` — colours picked
  for you, or supply `colour=[RED, CYAN]`.
- **Styles**: `style="line"` (default), `"scatter"` (clouds of
  points — correlation's natural dress), `"bar"` (counts and
  spectra).
- **Overlays**: `clear=False` draws on top of what's there — theory
  over experiment is one extra call.

## Arrays: the loops vanish

A ulab array is a list that took physics. Import the dialect and make
one:

```python
import ulab.numpy as np

x = np.linspace(0, 2 * math.pi, 128)    # 128 evenly spaced values
y = np.sin(x)                           # sin of ALL of them, at once
plot(y, x=x)
```

Read the middle line again: `np.sin(x)` computed 128 sines with no
`for` in sight — the loop happens inside, in C, at machine speed.
That is the whole philosophy: **operations apply to every element at
once**, and they compose like ordinary arithmetic:

```python
y2 = np.sin(2 * x) * 0.5 + 0.1          # scaled, shifted, all at once
plot([y, y2], x=x)
```

Arrays also answer questions about themselves — `np.mean(y)`,
`np.std(y)`, `np.min`, `np.max`, `np.sort` — and slice like lists
(`y[10:20]`, chapter 10 unchanged). For elementwise work on big data,
the speedup over a Python loop is roughly *a hundredfold* — and
experiment 2 has you measure it rather than believe it.

## Where the rest lives

This chapter uses the daily tenth of the library; the other
nine-tenths — 2-D matrices, `np.linalg` (inverses, determinants,
eigenvalues), `np.fft`, interpolation, complex arrays, and the
`ulab.scipy` corner (signal filtering, special functions, solvers) —
are real, aboard, and documented. Three signposts, in the order to
try them:

- **On the machine**: `help(np)` — chapter 3's trick — lists every
  function *your* firmware actually carries, no internet required.
  It is the authoritative inventory (and the User Manual's section 17
  gives the guided summary).
- **The ulab documentation** — **https://micropython-ulab.readthedocs.io/**
  — is the definitive reference for exactly this library: every
  function with examples, written for microcontroller Python. When
  something here surprises you, this is where the answer is.
- **The NumPy mothership** — **https://numpy.org/learn/** — for the
  wider world: tutorials, the *Absolute Beginner's Guide*, and the
  habits of thought behind array programming. Because ulab speaks
  NumPy's dialect, most of what you learn there works here unchanged
  — with the honest caveat that ulab is a deliberate *subset* (arrays
  stop at 2-D, some exotica are absent). The translation rule: learn
  the idea from NumPy's docs, confirm the spelling in ulab's.

That third link is the door this chapter opened: the same `np.`
you use on this shelf-top machine runs the world's data science, and
nothing you learn about it is ever wasted.

## The `pcmath` bench

Four tools from the board's own kit, chosen for the lab below:

- **`pcmath.correl(a, b)`** — the Pearson correlation coefficient:
  how much do two datasets move together, from −1 through 0 to +1?
  The single most-quoted number in empirical science, computed in one
  call.
- **`pcmath.crossings(a, level=0)`** — where a signal crosses a
  level. Counting zero-crossings of one second of samples is a
  frequency meter made of almost nothing.
- **`pcmath.power_spectrum(a)`** — *what frequencies live in this
  signal?* The Fourier transform, served as `|FFT|²`: feed it N
  samples (N a power of two) taken over one second and slot `k` of
  the result is the loudness of `k` Hz. The lab makes this magic
  tangible. (Its companion `pcmath.window(n)` tapers a signal's ends
  for cleaner spectra — the manual has the whys.)
- **`pcmath.chi_square(observed, expected)`** — is this die loaded,
  or is that just luck? Returns the statistic *and* the p-value.
  Experiment 4 puts a suspect die on trial.

(The bench also holds 3-D vectors and quaternions — `vcross`, `vdot`,
`vrotate`, `pcmath.Quat` — waiting for the day your programs leave
flatland, and a `PID` controller that chapter 31's world of motors is
the natural home for.)

> **Coming from MMBasic:** the `MATH` command family landed here —
> `MATH CORREL`/`CHI` are `pcmath.correl`/`chi_square`, the FFT and
> window verbs are `power_spectrum`/`window`, the `V_`/`Q_` vector
> and quaternion verbs are the `pcmath` names above — and MMBasic
> arrays grew up into ulab, where the maths applies itself.

## Project: the maths lab

One program, four experiments, chosen from a menu — the machinery is
chapter 10's dict-of-functions and chapter 11 throughout, and each
experiment ends with a picture. `edit("mathlab.py")`:

```python
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
```

Run all four. The pedagogy hiding in each:

- **The grapher** is `plot`'s function form wearing a menu — and the
  `zoo` dict holds *lambdas*, chapter 27's small functions now
  standing beside `math.sin` as equals. Note `squarish`: three sine
  waves summing toward a square wave — Fourier's whole idea, drawn,
  three chapters before anyone says "Fourier".
- **The statistics** experiment is your `scorelib` data's future:
  type in the last week's scores and meet your own mean and standard
  deviation. (`np.std` — the "give or take" number — may be the most
  useful statistic a household ever adopts.)
- **The signal detective** is the chapter's magic trick, played
  honestly: a sine buried in noise you can *see* is hopeless, and
  one `power_spectrum` later a single bar stands accusing. Wave
  physics, forensic audio and JPEG compression are all downstream of
  this one picture. (The noise occasionally wins at low signal
  strengths — that's real too, and the reroll is one keypress.)
- **The kinship** experiment fabricates its data *with a known
  relationship plus noise* — which is precisely how you learn what
  `r = 0.9` versus `r = 0.4` *looks like* as a cloud. Intuition for
  scatter plots is a scientist's reflex; twenty runs of this build
  it.

## Experiments

1. Overlay theory on experiment: in the detective, after the
   spectrum, `plot(sig, clear=True)` then a *pure* sine of the
   caught frequency with `clear=False` on top. How well does the
   guess hug the noise?
2. The promised speed race: square 10,000 numbers with a Python
   `for` loop, then as `arr * arr` — `ticks_diff` around each
   (chapter 21). Report the ratio. Then try 100,000 and mind the
   RAM (chapter 34 will have things to say).
3. Chapter 20's beat frequencies, seen at last: build
   `np.sin(2 * math.pi * 440 * t)` plus the same at 444 Hz over a
   tenth of a second, and plot the sum — the wobble you *heard* in
   the stereo lab, now visible as an envelope.
4. The dice court: roll `random.randint(1, 6)` six hundred times,
   tally into six counts (chapter 10), and try
   `pcmath.chi_square(counts, [100] * 6)`. A p-value above 0.05
   acquits. Now rig the die (a 7th chance of rolling six...) and
   watch the p-value convict.
5. `pcmath.crossings` as a frequency meter: in the detective, count
   zero crossings of the *raw noisy signal* and halve — how close
   does this two-line method get to the FFT's answer, and when does
   it fall apart? (Both methods are used in real instruments; now
   you know their temperaments.)

## Challenges

1. **Numerical calculus.** For `y = np.sin(x)`: the slope is
   `(y[1:] - y[:-1]) / dx` (array slicing does calculus!), the area
   a running sum. Plot the slope over the original — it's the
   cosine, discovered rather than memorised. Newton, on a shelf.
2. **The projectile gallery.** One function: range of a thrown ball
   vs angle. Plot the trajectory *family* (five angles, one plot,
   `clear=False`) and mark 45°'s crown. Then add air drag as a
   fudge factor and watch the crown slip below 45° — as it does on
   real fields.
3. **Lissajous art.** `plot(np.sin(3 * t), x=np.sin(2 * t),
   style="scatter")` — then vary the two integers. Physics-lab
   oscilloscope art, one line each; frame the best (chapter 15's
   `save_image` still works under a plot).
4. **The lab notebook.** Marry this chapter to chapter 29's weather
   diary: after a week of CSV logging, load the columns
   (chapter 12), plot temperature against hour-of-day as a scatter,
   and put `correl` on the case: does *your* street warm with the
   clock? You are, at this point, simply doing science.
