# INTERACTIVE: audio — tones, the 4-voice synth, pause/resume, and (if asset
# files are present) MOD/MP3 playback. The human confirms what they heard.

import time
import pcaudio
import testutil as T

T.section("audio (interactive)")
T.prompt("\n== audio test: listen to the speakers/headphones ==")
pcaudio.volume(70)

# Tone: fixed pitch, then a live retune (must be a smooth glide, no clicks).
pcaudio.tone(440, 440, 800, wait=True)
T.check(not pcaudio.is_playing(), "timed tone ended by itself")
T.check(T.ask("Did you hear a short steady tone?"), "tone audible")

for f in (262, 294, 330, 349, 392, 440, 494, 523):
    pcaudio.tone(f)
    time.sleep_ms(180)
pcaudio.stop()
T.check(T.ask("Did you hear a rising scale WITHOUT clicks between notes?"),
        "live retune is click-free")

# Synth: two voices, then white noise.
pcaudio.sound(1, "L", "Q", 220)
pcaudio.sound(2, "R", "S", 442)
time.sleep(1)
pcaudio.sound(1, "L", "O", 1)
pcaudio.sound(2, "R", "O", 1)
pcaudio.sound(3, "B", "N", 2000)
time.sleep(1)
pcaudio.stop()
T.check(T.ask("Square wave left / sine right, then noise on both?"),
        "synth voices and sides")

# Pause / resume state machine (audible gap).
pcaudio.tone(440)
time.sleep_ms(400)
pcaudio.pause()
T.check(pcaudio.is_playing(), "paused playback still registered as active")
time.sleep_ms(600)
pcaudio.resume()
time.sleep_ms(400)
pcaudio.stop()
T.check(T.ask("Did the tone pause and come back?"), "pause/resume")

# Optional file playback (drop test.mod / test.mp3 next to the tests).
import os

for fname in ("test.mod", "test.mp3"):
    try:
        os.stat(fname)
    except OSError:
        print("  skip:", fname, "not present")
        continue
    title = pcaudio.play(fname)
    if fname.endswith(".mod"):
        T.check(isinstance(title, str), "MOD title returned (%s)" % title)
        time.sleep(2)
        pcaudio.mod_sample(1)  # any instrument, mixed over the music
        time.sleep(1)
        pcaudio.stop()
        T.check(T.ask("Music played, with an extra sound effect over it?"),
                "MOD + mod_sample")
    else:
        time.sleep(2)
        pcaudio.stop()
        T.check(T.ask("Did the MP3 play?"), "MP3 playback")

if __name__ == "__main__":
    T.report()
