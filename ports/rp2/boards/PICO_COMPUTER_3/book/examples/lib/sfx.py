# sfx.py -- game sound effects.  import sfx; sfx.coin()
import time
from pcaudio import sound, tone, stop

def laser():
    for f in range(2000, 200, -150):        # a fast falling sweep
        sound(1, "B", "Q", f, 20)
        time.sleep(0.01)
    sound(1, "B", "O", 1)

def boom():
    for v in range(25, 0, -2):              # noise, fading out
        sound(4, "B", "N", 400, v)
        time.sleep(0.03)
    sound(4, "B", "O", 1)

def jump():
    for f in range(200, 900, 60):           # a quick rising chirp
        sound(2, "B", "T", f, 18)
        time.sleep(0.008)
    sound(2, "B", "O", 1)

def coin():
    tone(988, 988, 80, wait=True)           # the two-note classic
    tone(1319, 1319, 200, wait=True)

def siren(times=3):
    for _ in range(times):
        for f in (list(range(600, 1200, 25))
                  + list(range(1200, 600, -25))):
            sound(3, "B", "S", f, 16)
            time.sleep(0.006)
    sound(3, "B", "O", 1)

if __name__ == "__main__":
    import keyboard
    from keyboard import keydown
    print("SFX BOARD -- 1 laser  2 boom  3 jump  4 coin  5 siren "
          " Esc quits")
    while True:
        k = keydown(1)
        if k == ord("1"):
            laser()
        elif k == ord("2"):
            boom()
        elif k == ord("3"):
            jump()
        elif k == ord("4"):
            coin()
        elif k == ord("5"):
            siren(1)
        elif k == keyboard.ESC:
            break
        time.sleep(0.02)
    stop()
