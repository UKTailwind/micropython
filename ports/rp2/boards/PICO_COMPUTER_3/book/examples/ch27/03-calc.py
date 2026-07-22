import time
import pcgui

screen(hdmi.RGB320)
time.sleep(3)
console("serial")

hdmi.fb().fill(0)
g = pcgui.GUI()
g.start()
done = [False]

disp = g.displaybox(20, 12, 280, 36, "0", font=3)

entry = ["0"]                       # what's being typed
acc = [None]                        # the running total
op = [None]                         # the pending operator
# next digit starts a new number
fresh = [True]

def show(text):
    disp.value = text[:14]

def calc(a, o, b):
    try:
        if o == "+":
            r = a + b
        elif o == "-":
            r = a - b
        elif o == "x":
            r = a * b
        else:
            r = a / b
    except ZeroDivisionError:
        return None
    return r

def fmt(x):
    if x is None:
        return "Err"
    if x == int(x):
        return str(int(x))
    return f"{x:.6f}".rstrip("0")

def press(c):
    if c == "C":
        entry[0], acc[0], op[0], fresh[0] = "0", None, None, True
    elif c in "0123456789.":
        if fresh[0]:
            entry[0], fresh[0] = "", False
        if c != "." or "." not in entry[0]:
            entry[0] += c
        entry[0] = entry[0] or "0"
    elif c in "+-x/":
        if op[0] is not None and not fresh[0]:
            acc[0] = calc(acc[0], op[0], float(entry[0]))
            entry[0] = fmt(acc[0])
        elif acc[0] is None:
            acc[0] = float(entry[0])
        op[0] = c
        fresh[0] = True
        # a division just blew up
        if acc[0] is None:
            entry[0], op[0] = "Err", None
    elif c == "=" and op[0] is not None and not fresh[0]:
        acc[0] = calc(acc[0], op[0], float(entry[0]))
        entry[0] = fmt(acc[0])
        acc[0], op[0], fresh[0] = None, None, True
    show(entry[0])
    beep(660, 8)

def power_off(b):
    done[0] = True

ROWS = ["789/", "456x", "123-", "C0.+"]
for r, row in enumerate(ROWS):
    for cidx, ch in enumerate(row):
        g.button(20 + cidx * 72, 64 + r * 40, 62, 32, ch, font=2,
                 callback=lambda b, c=ch: press(c))
g.button(236, 184, 62, 32, "=", font=2, bg=COBALT,
         callback=lambda b: press("="))
g.button(236, 64, 62, 32, "OFF", fg=WHITE, bg=RED,
         callback=power_off)

try:
    while not done[0]:
        g.poll()
        time.sleep_ms(10)
finally:
    g.stop()
    console()
