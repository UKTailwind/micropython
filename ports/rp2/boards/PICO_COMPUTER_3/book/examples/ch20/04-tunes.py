import time

NOTES = {"C": 262, "D": 294, "E": 330, "F": 349,
         "G": 392, "A": 440, "B": 494, "C5": 523}

ODE = [("E", 1), ("E", 1), ("F", 1), ("G", 1),
       ("G", 1), ("F", 1), ("E", 1), ("D", 1),
       ("C", 1), ("C", 1), ("D", 1), ("E", 1),
       ("E", 1.5), ("D", 0.5), ("D", 2)]

def play_tune(melody, bpm=120):
    beat = 60 / bpm
    for note, beats in melody:
        ms = int(beats * beat * 1000)
        # a rest: silence, same length
        if note == "R":
            time.sleep(ms / 1000)
        else:
            tone(NOTES[note], NOTES[note], ms, wait=True)
        # a breath between notes
        time.sleep(0.02)

play_tune(ODE, 140)
