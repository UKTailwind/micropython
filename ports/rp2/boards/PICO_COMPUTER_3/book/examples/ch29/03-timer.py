import time
import pcgui

screen(hdmi.RGB320)
time.sleep(3)
console("serial")

hdmi.fb().fill(0)
g = pcgui.GUI()
g.start()
done = [False]
# None, or (start_ms, total_ms)
running = [None]

g.caption(96, 8, "KITCHEN TIMER", fg=GOLD, font=2)
g.caption(24, 58, "minutes")
mins = g.spinner(24, 72, 90, 28, value=5, lo=1, hi=120, step=1)
disp = g.displaybox(150, 56, 146, 44, "05:00", font=3)
bar = g.bargauge(20, 116, 280, 16, value=0, lo=0, hi=100,
                 fg=GREEN)

def fmt_ms(ms):
    s = max(0, ms) // 1000
    return f"{s // 60:02}:{s % 60:02}"

def start(b):
    total = int(mins.number) * 60000
    running[0] = (time.ticks_ms(), total)
    disp.value = fmt_ms(total)

def cancel(b):
    running[0] = None
    bar.value = 0
    disp.value = fmt_ms(int(mins.number) * 60000)

def quit_app(b):
    done[0] = True

g.button(30, 150, 120, 34, "START", fg=WHITE, bg=COBALT,
         callback=start)
g.button(170, 150, 120, 34, "CANCEL", callback=cancel)
g.button(12, 202, 70, 26, "EXIT", callback=quit_app)

try:
    while not done[0]:
        g.poll()
        if running[0]:
            t0, total = running[0]
            left = total - time.ticks_diff(time.ticks_ms(), t0)
            disp.value = fmt_ms(left)
            bar.value = min(100, 100 * (total - max(0,
                            left)) // total)
            if left <= 0:
                running[0] = None
                disp.value = "DING!"
                for _ in range(3):
                    tone(880, 880, 160, wait=True)
                    tone(1318, 1318, 240, wait=True)
        time.sleep_ms(50)
finally:
    g.stop()
    console()
