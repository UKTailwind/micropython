# initials.py -- arcade name entry.  import initials; name =
# initials.get()
import hdmi
import pcgame
import keyboard
from keyboard import keydown
from pcaudio import beep
from pcgfx import WHITE, GRAY, GOLD

def _held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

def get(title="ENTER YOUR INITIALS"):
    """Three letters, arcade style: LEFT/RIGHT spin, SPACE locks.
    Draws over whatever is on screen; returns e.g. 'ADA'."""
    d = hdmi.fb()
    W = hdmi.width()
    letters = [0, 0, 0]
    slot = 0
    # keys may still be held (ch 25!)
    l_was = r_was = f_was = True
    clock = pcgame.Clock(vsync=True)
    while True:
        clock.tick()
        l_now, r_now = _held(keyboard.LEFT), _held(keyboard.RIGHT)
        f_now = _held(ord(" "))
        if r_now and not r_was:
            letters[slot] = (letters[slot] + 1) % 26
        if l_now and not l_was:
            letters[slot] = (letters[slot] - 1) % 26
        if f_now and not f_was:
            slot += 1
            beep(660 + slot * 110, 30)
            if slot == 3:
                return "".join(chr(65 + n) for n in letters)
        l_was, r_was, f_was = l_now, r_now, f_now

        x0 = W // 2 - 120
        d.fill_rect(x0 - 20, 150, 280, 160, d.colour(0x101828))
        hdmi.text(title, W // 2 - len(title) * 4, 165,
                  d.colour(GRAY))
        for i in range(3):
            colr = (d.colour(GOLD) if i == slot
                    else d.colour(WHITE))
            hdmi.text(chr(65 + letters[i]), x0 + i * 90, 200,
                      colr, -1, 2, 5)
        d.fill_rect(x0 + slot * 90, 275, 48, 4, d.colour(GOLD))

if __name__ == "__main__":
    print("demo: spin with LEFT/RIGHT, lock with SPACE")
    name = get()
    print("welcome,", name)
