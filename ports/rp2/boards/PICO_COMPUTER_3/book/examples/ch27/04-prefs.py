import time
import pcgui

PREFS = "/prefs.txt"
DEFAULTS = {"sound": 1, "flash": 1, "volume": 70, "level": 2}

def load_prefs():
    prefs = dict(DEFAULTS)
    try:
        with open(PREFS) as f:
            for line in f:
                key, _, val = line.strip().partition("=")
                if key in prefs:
                    prefs[key] = int(val)
    except (OSError, ValueError):
        pass                        # missing or mangled: defaults stand
    return prefs

def save_prefs(prefs):
    with open(PREFS, "w") as f:
        for key, val in prefs.items():
            f.write(f"{key}={val}\n")

prefs = load_prefs()

screen(hdmi.RGB320)
time.sleep(3)
console("serial")

hdmi.fb().fill(0)
g = pcgui.GUI()
g.start()
done = [False]

def setter(key):
    """Make a callback that files a control's value under `key`."""
    def apply(c):
        prefs[key] = c.value
    return apply

def chooser(key, val):
    """Make a callback that files the fixed `val` (for radio buttons)."""
    def apply(c):
        prefs[key] = val
    return apply

def quit_app(b):
    done[0] = True

g.caption(90, 8, "GAME SETTINGS", fg=GOLD, font=2)

g.frame(12, 34, 296, 74, "Effects")
g.switch(24, 52, 80, 24, "ON|OFF", value=prefs["sound"],
         callback=setter("sound"))
g.caption(110, 58, "sound")
g.switch(24, 80, 80, 24, "ON|OFF", value=prefs["flash"],
         callback=setter("flash"))
g.caption(110, 86, "screen flash")

g.frame(12, 114, 296, 52, "Volume")
vol = g.slider(24, 134, 200, 20, value=prefs["volume"], lo=0, hi=100,
               callback=setter("volume"))
g.bargauge(240, 134, 56, 20, value=prefs["volume"], lo=0, hi=100, fg=GREEN)

g.frame(12, 172, 200, 60, "Difficulty")
for i, name in enumerate(("easy", "normal", "fierce")):
    g.radio(28 + i * 62, 200, 8, name, group=1,
            value=1 if prefs["level"] == i + 1 else 0,
            callback=chooser("level", i + 1))

saved = g.caption(226, 180, "", fg=GREEN)

def do_save(b):
    save_prefs(prefs)
    saved.value = "saved!"
    beep(880, 40)

g.button(224, 196, 84, 30, "SAVE", fg=WHITE, bg=COBALT, callback=do_save)
g.button(12, 4, 56, 24, "EXIT", callback=quit_app)

try:
    while not done[0]:
        g.poll()
        time.sleep_ms(10)
finally:
    g.stop()
    console()
    print("prefs:", prefs)
