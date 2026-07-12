# Pico Computer 3 — on-device test suite

Test programs for the board-specific functionality (the custom development on
top of stock MicroPython). They run **on the board**.

## Running

Copy this `tests/` directory to the SD card (or the flash filesystem), then:

```python
cd("/sd/tests")
run("test_all.py")
```

`test_all.py` runs the **automatic** suites first — they verify themselves by
reading pixels back from the framebuffers and checking API state, and route
console output to the serial port while they draw (`console("serial")`) so
the screen isn't disturbed mid-test. It then offers the **interactive**
suites, which need a human (press keys, listen, watch the screen).

Each file also runs standalone: `run("test_blit.py")` etc.

| File | Kind | Covers |
|---|---|---|
| `test_blit.py` | automatic | `hdmi.blit`: copy/skip-colour/overlap/tuple surfaces/clipping, in all 4 video modes |
| `test_buffers.py` | automatic | layer + F buffer: `layer/create/write/copy/close/transparent`, lifecycle errors, mode-change teardown |
| `test_sprites.py` | automatic | `pcsprite`: draw/erase/z-order (pixel-verified), collisions (sprite/edge/wall/layers, edge-triggered), `scroll` incl. wrap, both compositor modes |
| `test_images.py` | automatic | `save_image`/`draw_bmp` pixel round-trip in 3 modes; optional `test.jpg`/`test.png` decode |
| `test_misc.py` | automatic | RTC, settings, shell helpers, keydown/mouse/touch queries, audio state machine, SD info |
| `test_keydown.py` | interactive | `keydown()` codes/modifiers/multi-key, `keyboard.on_key` |
| `test_audio.py` | interactive | tone (incl. click-free retune), 4-voice synth, pause/resume; optional `test.mod`/`test.mp3` |
| `test_console.py` | interactive | `console("both"/"serial"/"screen")` output routing |

Optional test assets (put next to the tests, all skipped if absent):
`test.jpg`, `test.png`, `test.mod`, `test.mp3`.

At the end the runner restores the saved screen mode and prints a summary;
anything in the FAIL list is a regression (or a new bug — see
`DEVELOPMENT_NOTES.md` for what each feature is supposed to do).
