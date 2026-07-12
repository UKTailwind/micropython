# Audio player/synthesiser for the Pico Computer 3, over the PCM5102 I2S DAC.
#
# Playback runs in the BACKGROUND: machine.I2S(0) is put in non-blocking mode
# (i2s.irq), so i2s.write() returns immediately and a scheduler callback (_feed)
# queues each successive chunk. The REPL stays live while audio plays. I2S(0)
# runs on DMA_IRQ_0 (shared with rp2.DMA) so it never collides with HDMI's
# exclusive DMA_IRQ_1. The per-sample volume scaling is done in C (audio.scale).
#
# play(path)         .wav/.mp3/.flac files, and .mod tracker music (hxcmod)
# mod_sample(...)    sound effect mixed OVER a playing MOD (MMBasic MODSAMPLE)
# tone(l, r, ms)     dual sine tones (MMBasic PLAY TONE); live-retunable
# sound(v, ...)      4-voice synth: sine/square/triangle/saw/noise waveforms
#                    per voice and side (MMBasic PLAY SOUND)
# volume(v)          get/set volume 0..100 (perceptual / log taper)
# beep(...)          short synthesised tone
# pause() / resume() suspend and continue the current playback
# stop() / is_playing()
# deinit()           release the I2S peripheral

import math
import struct
import time
from machine import I2S, Pin

import audio  # C helpers: scale, decoders, tone/sound synth, hxcmod MOD player

# PCM5102 wiring on the Pico Computer 3 (LRCK is BCLK+1 = GP11).
_BCLK = 10
_DIN = 22

_i2s = None
_i2s_cfg = None
_vol = 80
_gain = 256
_pb = None  # active playback: {"produce", "f", "i2s", "kind"}  or None when idle
_paused = False
_pending = False  # a non-blocking i2s.write is outstanding


def _compute_gain(v):
    # Map 0..100 to an 8.8 fixed-point gain (0..256) on a ~50 dB log taper, so
    # perceived loudness tracks the setting rather than the raw amplitude.
    if v <= 0:
        return 0
    if v >= 100:
        return 256
    return max(0, min(256, round(256 * 10 ** ((v - 100) / 40.0))))


def volume(v=None):
    """Get the volume (no arg) or set it to 0..100."""
    global _vol, _gain
    if v is None:
        return _vol
    _vol = max(0, min(100, int(v)))
    _gain = _compute_gain(_vol)
    return _vol


def _feed(i2s):
    # I2S non-blocking callback (runs on the scheduler): queue the next chunk.
    global _pending
    _pending = False  # the previous write (if any) has completed
    st = _pb
    if st is None or _paused:
        return  # paused: the chain stops here; resume() re-primes it
    chunk = st["produce"]()
    if chunk is None:
        _finish()  # end of stream
        return
    _pending = True
    i2s.write(chunk)


def _finish():
    # End of stream (natural): stop feeding and release the file/decoder, but
    # leave the I2S to play out its buffered tail (the true end of the sound).
    global _pb
    st = _pb
    _pb = None
    if st is not None:
        close = st.get("close")
        if close is not None:
            close()  # dr_wav / dr_mp3 uninit
        if st["f"] is not None:
            try:
                st["f"].close()
            except Exception:
                pass


def stop():
    """Stop playback immediately (drops the buffered tail)."""
    global _paused, _pending
    _paused = False
    _pending = False
    _finish()
    global _i2s, _i2s_cfg
    if _i2s is not None:
        _i2s.deinit()  # abort DMA now, rather than draining ~85 ms of buffer
        _i2s = None
        _i2s_cfg = None


def pause():
    """Suspend the current playback (the short buffered tail plays out).
    resume() continues from the same point. MMBasic PLAY PAUSE."""
    global _paused
    if _pb is not None:
        _paused = True


def resume():
    """Continue a paused playback. MMBasic PLAY RESUME."""
    global _paused
    if _pb is not None and _paused:
        _paused = False
        if not _pending:  # the feed chain has fully stopped: re-prime it
            _feed(_pb["i2s"])


def is_playing():
    return _pb is not None


# A fresh I2S per playback avoids racing a new write against an in-flight
# non-blocking copy; end-of-file stops cleanly (no deinit), so only an explicit
# switch cuts the previous sound.
def _new_i2s(rate, channels, ibuf=16384):
    global _i2s, _i2s_cfg
    if _i2s is not None:
        _i2s.deinit()
    _i2s = I2S(
        0,
        sck=Pin(_BCLK),
        ws=Pin(_BCLK + 1),
        sd=Pin(_DIN),
        mode=I2S.TX,
        bits=16,
        format=I2S.STEREO if channels == 2 else I2S.MONO,
        rate=rate,
        ibuf=ibuf,
    )
    _i2s.irq(_feed)  # enable non-blocking (background) mode
    _i2s_cfg = (rate, 16, channels)
    return _i2s


def deinit():
    """Stop playback and release the I2S peripheral (DMA/PIO)."""
    stop()
    global _i2s, _i2s_cfg
    if _i2s is not None:
        _i2s.deinit()
        _i2s = None
        _i2s_cfg = None


# extension -> (open, read, close) decoder trio (dr_wav / dr_mp3, in C).
_DECODERS = {
    "wav": (audio.wav_open, audio.wav_read, audio.wav_close),
    "mp3": (audio.mp3_open, audio.mp3_read, audio.mp3_close),
    "flac": (audio.flac_open, audio.flac_read, audio.flac_close),
}


def play(path, wait=False, loop=False):
    """Play a .wav, .mp3, .flac or .mod in the background (decoded to 16-bit
    PCM in C). wait=True blocks until it finishes. loop=True repeats a .mod
    forever (as MMBasic PLAY MODFILE does). Returns the song title for .mod."""
    stop()
    ext = path.rsplit(".", 1)[-1].lower() if "." in path else ""
    title = None
    if ext == "mod":
        title = _play_mod(path, loop)
    else:
        dec = _DECODERS.get(ext)
        if dec is None:
            raise ValueError("unsupported audio format: " + ext)
        d_open, d_read, d_close = dec
        f = open(path, "rb")
        try:
            ch, rate = d_open(f)  # decoder reads the header
        except Exception:
            f.close()
            raise
        i2s = _new_i2s(rate, ch)
        buf = bytearray(4096)
        full = memoryview(buf)

        def produce():
            n = d_read(buf)  # decode next frames as 16-bit interleaved PCM
            if not n:
                return None
            mv = full if n == len(buf) else full[:n]
            audio.scale(mv, _gain)  # apply volume in C
            return mv

        global _pb
        _pb = {"produce": produce, "f": f, "close": d_close, "i2s": i2s, "kind": ext}
        _feed(i2s)  # prime the first chunk
    if wait:
        while _pb is not None:
            time.sleep_ms(10)
    return title


def _play_mod(path, loop):
    # MOD tracker playback (hxcmod in C, from MMBasic). The whole file is read
    # into RAM (PSRAM heap) because the tracker plays its instrument samples in
    # place; the bytes object is kept referenced in _pb for the GC. hxcmod
    # renders 16-bit stereo at 22050 Hz (MMBasic's modfilesamplerate).
    with open(path, "rb") as f:
        data = f.read()
    title = audio.mod_open(data, loop)
    # Smaller chunk/ibuf than file playback so mod_sample() effects start
    # reasonably quickly (~46 ms chunk + ~93 ms queue at 22050 Hz).
    i2s = _new_i2s(22050, 2, ibuf=8192)
    buf = bytearray(4096)
    mv = memoryview(buf)

    def produce():
        n = audio.mod_read(buf)  # render the next tracker chunk
        if not n:
            return None
        audio.scale(mv, _gain)
        return mv

    global _pb
    _pb = {"produce": produce, "f": None, "data": data, "close": audio.mod_close,
           "i2s": i2s, "kind": "mod"}
    _feed(i2s)
    return title


def mod_sample(sample, effect=1, vol=64, rate=16000):
    """Play instrument sample 1..32 of the currently playing MOD as a sound
    effect mixed over the music, on effect channel 1..4 (MMBasic MODSAMPLE).
    vol 1..64; rate in Hz (MMBasic uses 16000)."""
    audio.mod_sample(sample, effect, vol, rate)


def tone(f_left, f_right=None, ms=None, wait=False):
    """Play sine tones: f_left/f_right in Hz (0 = silence), for ms milliseconds
    (None = until stop()). Calling tone() again while a tone is playing
    retunes it seamlessly — chords/arpeggios work like MMBasic PLAY TONE."""
    global _pb
    if f_right is None:
        f_right = f_left
    if _pb is not None and _pb.get("kind") == "tone":
        audio.tone_start(f_left, f_right, ms, False)  # live retune, no restart
    else:
        stop()
        audio.tone_start(f_left, f_right, ms, True)
        i2s = _new_i2s(44100, 2, ibuf=4096)  # small queue: retunes are heard fast
        buf = bytearray(2048)
        mv = memoryview(buf)

        def produce():
            n = audio.tone_read(buf)
            if not n:
                return None
            out = mv if n == len(buf) else mv[:n]
            audio.scale(out, _gain)
            return out

        _pb = {"produce": produce, "f": None, "i2s": i2s, "kind": "tone"}
        _feed(i2s)
    if wait and ms is not None:
        while _pb is not None:
            time.sleep_ms(5)


# MMBasic PLAY SOUND waveform letters -> audio module waveform ids.
_WAVES = {
    "O": audio.OFF,       # off
    "S": audio.SINE,
    "Q": audio.SQUARE,
    "T": audio.TRIANGLE,
    "W": audio.SAW,
    "P": audio.PNOISE,    # periodic noise
    "N": audio.WNOISE,    # white noise
}


def sound(voice, side, wave, freq=10.0, vol=25):
    """Set one synthesiser voice (MMBasic PLAY SOUND): voice 1..4; side "L",
    "R" or "B"(oth); wave "S"ine, "Q"(square), "T"riangle, "W" (sawtooth),
    "P"(periodic noise), "N" (white noise) or "O"ff; freq in Hz; vol 0..25.
    The synth runs until stop(); voices can be changed live."""
    global _pb
    lft = str(side).upper() in ("L", "B", "M")
    rgt = str(side).upper() in ("R", "B", "M")
    if not (lft or rgt):
        raise ValueError("side must be L, R or B")
    w = _WAVES.get(str(wave).upper())
    if w is None:
        raise ValueError("wave must be one of O S Q T W P N")
    if _pb is not None and _pb.get("kind") == "sound":
        audio.sound_set(voice - 1, lft, rgt, w, freq, vol)  # live change
        return
    stop()
    audio.sound_reset()
    audio.sound_set(voice - 1, lft, rgt, w, freq, vol)
    i2s = _new_i2s(44100, 2, ibuf=4096)  # small queue: changes are heard fast
    buf = bytearray(2048)
    mv = memoryview(buf)

    def produce():
        audio.sound_read(buf)  # the synth never ends; stop() ends it
        audio.scale(mv, _gain)
        return mv

    _pb = {"produce": produce, "f": None, "close": audio.sound_reset,
           "i2s": i2s, "kind": "sound"}
    _feed(i2s)


# USB plug-in / unplug sounds. keyboard.on_usb_event() schedules system_sound()
# whenever a USB device is connected/removed (see _boot). Set usb_sounds = False
# to silence them.
usb_sounds = True


def system_sound(connect=True):
    """Play the short USB connect (True) / disconnect (False) sound in the
    background. Skipped while other audio is playing, so it never cuts off
    music (mirrors MMBasic's PlayMemWav)."""
    global _pb
    if not usb_sounds or _pb is not None:
        return
    data, rate = audio.usb_sound(bool(connect))  # 16-bit stereo PCM + rate (C)
    mv = memoryview(data)
    audio.scale(mv, _gain)  # apply the current volume
    i2s = _new_i2s(rate, 2)
    total = len(mv)
    pos = [0]

    def produce():
        p = pos[0]
        if p >= total:
            return None
        pos[0] = p + 4096
        return mv[p : p + 4096]  # feed in ibuf-sized chunks

    _pb = {"produce": produce, "f": None, "i2s": i2s}
    _feed(i2s)


def beep(freq=880, ms=150, rate=22050, wait=True):
    """Play a short synthesised tone (blocks by default; wait=False to return)."""
    stop()
    n = max(1, round(rate / freq))
    cyc = bytearray(n * 4)
    for i in range(n):
        v = int(18000 * math.sin(2 * math.pi * i / n)) * _gain >> 8
        struct.pack_into("<hh", cyc, i * 4, v, v)
    reps = max(1, round(freq * ms / 1000))
    i2s = _new_i2s(rate, 2)
    mv = memoryview(cyc)
    cnt = [0]

    def produce():
        if cnt[0] >= reps:
            return None
        cnt[0] += 1
        return mv

    global _pb
    _pb = {"produce": produce, "f": None, "i2s": i2s}
    _feed(i2s)
    if wait:
        while _pb is not None:
            time.sleep_ms(5)
