# Appendix H — Troubleshooting

Symptom → cause → cure. The chapter references hold the fuller story.

## Power and boot

| Symptom | Try |
|---|---|
| Nothing at all | The power switch is push-on/push-*off* — press once more. Then: is the supply 2 A-capable? (ch. 2) |
| No boot banner, monitor lit | You may be at a running `/main.py` — Ctrl-C. Still nothing: RESET; then serial console (below). |
| Boot takes ~10 s longer than usual | `auto(True)` NTP sync waiting for Wi-Fi (ch. 29). Normal; `auto(False)` if unwanted. |
| Machine reboots by itself, ~5 s after Ctrl-C | A watchdog you started is unfed (ch. 32). Reboot clears it. |

## Display

| Symptom | Try |
|---|---|
| "No signal" / unsupported mode | A persisted mode your monitor dislikes. Via serial console: `screen(hdmi.RGB640)` — or `rm("/settings.json")` and power-cycle. (ch. 2, 15) |
| Blank for a few seconds after `screen()` | Monitors take ~3 s to re-lock. Programs: `time.sleep(3)` after a mode change. (ch. 15) |
| Colours are wrong/swampy | A raw RGB number given to a drawing call — wrap it: `d.colour(0xRRGGBB)`. (ch. 15) |
| Drawing lands on the wrong screen / smears accumulate | `d = hdmi.fb()` taken *before* `hdmi.write("F")` — target first, Display second. (ch. 17) |
| `ValueError: framebuffer already exists` | A previous run left F open — `hdmi.close("F")` before `create()`. (ch. 17) |
| **Prompt has vanished entirely** | Console is routed to an invisible buffer or `"none"`. Type blind: `hdmi.write("N")` Enter, `console()` Enter. Or RESET. (ch. 17) |
| Cursor blinking over full-screen graphics | That's the console's cursor — `console("none")` while drawing, `console()` after. (ch. 17) |
| Game layout broken / text off-screen | Program assumed a mode it didn't set — add `screen(hdmi.RGB640)` (+ sleep) at the top. (ch. 23) |

## Keyboard, mouse, touch

| Symptom | Try |
|---|---|
| Keyboard dead | **USB HUB switch to ENABLE** (it's at DISABLE after flashing). Look for `USB keyboard -> slot 1` at boot. (ch. 2) |
| Wrong symbols (`"` vs `@`) | `keymap("UK")` — persisted. (ch. 2) |
| No mouse pointer visible | `pccursor.on()` outside a GUI; inside, `g.start()` shows it when a mouse is present. (ch. 27) |
| Touch gives −1 | Nothing touching — that's the sentinel; check `touch("DOWN")` first. Panels also need ~2 s after plug-in. (ch. 21) |
| Keys "stick" between game rounds | Poll for silence first — the `flush()` idiom. (ch. 21) |

## Files and SD

| Symptom | Try |
|---|---|
| `OSError: [Errno 2] ENOENT` | Path/file doesn't exist — `ls()` the folder; check `/sd/` prefix. (ch. 12) |
| `/sd` missing | Card seated? FAT-formatted? It appears/disappears live. (ch. 4) |
| Board won't appear as a drive on a PC | It never does — by design. Use the SD card, `xsend`/`xrecv`, or BOOTSEL mode (firmware only). (ch. 2, 5) |
| Edited library changes ignored | Modules cache per session — **Ctrl-D**, run again. (ch. 11) |
| `ImportError` for your own module | Same folder as the program, or `/lib`; imports take names, not paths. (ch. 11) |

## Sound

| Symptom | Try |
|---|---|
| Silence | Volume: `volume(60)`. Jack seated? Powered speakers on? |
| MOD effects don't play | `mod_sample` needs the `.mod` *currently playing*. (ch. 20) |

## Network

| Symptom | Try |
|---|---|
| `wifi()` fails / no sync | Credentials saved? (`wifi("ssid", "pw")` once.) In range? 2.4 GHz network? |
| `requests` raises `OSError` | The network's weather — that's why every fetch wears `try/except OSError`. (ch. 29) |
| Long-running fetcher dies young | A response never closed — audit for `r.close()` / `finally`. (ch. 29) |

## Programs

| Symptom | Try |
|---|---|
| Traceback | Last line = what; deepest line naming *your* file = where. Chapter 13's gallery has the culprit table. |
| Stuck program | Ctrl-C. Then RESET. Files always survive. (ch. 2) |
| Runs but wrong | Chapter 13's hunt: reproduce small → print the state → fix the cause → retest. |
| Stutters every few seconds | Garbage collection — allocate less per frame; `gc.collect()` at quiet moments. (ch. 33) |
| `MemoryError` with memory seemingly free | Fragmentation — pre-allocate big buffers at start-up. (ch. 33) |
| Handler/callback stopped firing | It raised once and died — wrap its body in `try/except`. (ch. 32) |
| Everything froze in an asyncio program | A blocking call in a task (`time.sleep`, `input`, a long loop) — every pause must be an `await`. (ch. 32) |

## When all else fails

Serial console via USB-C (115200) → Ctrl-C → investigate. Factory
settings: `rm("/settings.json")` + power-cycle. Reflash the firmware
(files survive). And the test suite (ch. 34) will tell you whether
the *machine* is fine and the problem is — as it usually is, for all
of us — the program.
