# Appendix B — Coming from MMBasic

The full translation table, gathered from every margin note in the
book. The right-hand column is this machine's idiom; chapter numbers
point to where each is taught. Three habits cover most of the
adjustment: **indentation replaces END-markers**, **`=` assigns and
`==` compares**, and **lower case matters** (`print`, never `PRINT`).

## Language

| MMBasic | Here | Ch. |
|---|---|---|
| `PRINT a; b` | `print(a, b)` — or an f-string | 3, 6 |
| `INPUT "Q", A` | `a = input("Q")` — **always a string**: wrap in `int()`/`float()` | 6 |
| `LET A = 1` / `A = 1` | `a = 1` | 6 |
| `INC A, 2` | `a += 2` | 6 |
| `A$ = "hi"` (type suffixes) | no suffixes — any variable holds anything | 6 |
| `IF ... THEN ... ELSE ... ENDIF` | `if ...:` / `elif` / `else:` — indentation is the block | 7 |
| `=` (comparison) / `<>` | `==` / `!=` | 7 |
| `AND OR NOT` (logic) | `and or not` | 7 |
| `FOR i = 1 TO 10 STEP 2 ... NEXT` | `for i in range(1, 11, 2):` — stop is *excluded* | 8 |
| `DO WHILE c ... LOOP` | `while c:` | 8 |
| `EXIT DO` / `EXIT FOR` | `break` | 8 |
| `CONTINUE DO` | `continue` | 8 |
| `SUB name ... END SUB` | `def name():` — call without `CALL` | 11 |
| `FUNCTION f(x) ... f = v` | `def f(x): ... return v` | 11 |
| `LOCAL x` | automatic — *everything* assigned in a `def` is local | 11 |
| `DIM a(10)` | `a = []` + `.append` — no size, 0-based, no `OPTION BASE` | 10 |
| structures (newer MMBasics) | tuples (positional) or classes (named fields + methods) | 10, 14 |
| `DATA` / `READ` / `RESTORE` | lists, walked with `for` | 10 |
| `GOTO` / `GOSUB` | absent — loops, functions and `state` variables instead | 8, 22 |
| `ON ERROR SKIP/IGNORE`, `MM.ERRNO` | `try:` / `except NameOfError as e:` | 13 |
| `END` | the program simply ends (or `break` out of the main loop) |  |
| `REM` / `'` | `#` | 5 |
| `^`, `\`, `MOD` | `**`, `//`, `%` | 3 |
| `PEEK` / `POKE` | `machine.mem32[...]` — rarely needed; handle with care |  |
| `CSUB` | `@micropython.native`, `@micropython.viper`, or a C module | 33 |

## Strings

| MMBasic | Here | Ch. |
|---|---|---|
| `LEN(s$)` | `len(s)` | 3 |
| `UCASE$` / `LCASE$` | `s.upper()` / `s.lower()` — returns a *copy* | 12 |
| `INSTR(s$, x$)` | `s.find(x)` (−1 if absent) — or `x in s` | 12 |
| `MID$(s$, 3, 4)` | `s[2:6]` — 0-based slicing | 12 |
| `LEFT$(s$, 4)` / `RIGHT$(s$, 4)` | `s[:4]` / `s[-4:]` | 12 |
| `CHR$(n)` / `ASC(c$)` | `chr(n)` / `ord(c)` | 12 |
| `STR$(n)` / `VAL(s$)` | `str(n)` / `int(s)`, `float(s)` | 6 |
| `FORMAT$(n, "%.2f")` | `f"{n:.2f}"` | 6 |

## Files and the shell

| MMBasic | Here | Ch. |
|---|---|---|
| drives `A:` / `B:` | paths `/` (flash) and `/sd` | 4 |
| `FILES` / `KILL` / `COPY` / `RENAME` | `ls()` / `rm()` / `cp()` / `mv()` | 4 |
| `MKDIR` / `CHDIR` | `mkdir()` / `cd()` | 4 |
| `OPEN f$ FOR INPUT AS #1` | `with open(f) as f:` | 12 |
| `OPEN ... FOR OUTPUT / APPEND` | `open(f, "w")` (wipes!) / `open(f, "a")` | 12 |
| `OPEN ... FOR RANDOM`, `SEEK`, `LOC()` | `open(f, "r+")`, `f.seek(n)`, `f.tell()` | 12 |
| `PRINT #1, x` | `f.write(text)` — add your own `\n` | 12 |
| `LINE INPUT #1, a$` | `for line in f:` (+ `.strip()`) | 12 |
| `EOF(#1)` | the `for` loop just ends | 12 |
| `CLOSE #1` | the `with` block's dedent — unforgettable by design | 12 |
| `RUN` / `EDIT` | `run("f.py")` / `edit("f.py")` | 5 |
| `AUTOSAVE` | `autosave("f.py")`, Ctrl-Z ends | 5 |
| `XMODEM SEND/RECEIVE` | `xsend()` / `xrecv()` | 5 |
| `FM` | `fm()` — plus Space multi-select | 4 |
| `OPTION AUTORUN` | `/main.py` (and `/boot.py` before it) | 5, 34 |
| `OPTION`s generally | `/settings.json` — set via `keymap()`, `screen()`, `wifi()`... | 34 |

## Screen and graphics

| MMBasic | Here | Ch. |
|---|---|---|
| `MODE` | `screen(hdmi.RGB640 / RGB320 / RGB512 / RGB1024)` — persisted | 15 |
| `CLS` | `cls()` (console) or `d.fill(c)` (graphics) | 3, 15 |
| `RGB(r,g,b)` / colour names | `0xRRGGBB` or the same 16 names + extras — **wrap in `d.colour()`** | 15 |
| `PIXEL LINE BOX RBOX CIRCLE ARC POLYGON` | `Display` methods: `pixel line rect/rbox ellipse arc poly` | 15 |
| `TEXT x, y, s$` / `FONT n` | `hdmi.text(s, x, y, fg, bg, scale, font)` — same font numbers | 15 |
| `PRINT @(x, y)` | `hdmi.text(...)` at the pixel | 15 |
| `LOAD JPG/PNG/BMP` | `draw_jpg/png/bmp(path)` (+ `dither=True`) | 16 |
| `SAVE IMAGE` | `save_image(path)` | 15 |
| `BLIT` | `hdmi.blit(x,y,w,h,x1,y1,src,dst,skip)` | 16 |
| `BLIT READ/WRITE` (memory frames) | `load_image()` → `Image.blit`/`.cell` | 16 |
| `FRAMEBUFFER N/L/F, WRITE, COPY, CLOSE` | `hdmi.layer()/create()/write()/copy()/close()` — same letters | 17 |
| `SPRITE` family (+ layers, collisions) | `import pcsprite` — objects; events from `sp.update()` | 18 |
| `TILEMAP` | `TileMap` — 0 = empty, 1-based tiles, attrs, `collide` | 19 |
| `TURTLE` | `Turtle()` — same headings, home = centre | 9 |
| `GUI` controls, `CTRLVAL(r)` | `pcgui` — `control.value` | 27 |
| `GUI INTERRUPT` | `g.on_touch(down=, up=, move=)` | 27 |
| `GUI CURSOR` | `pccursor.on()/refresh()/off()` | 27 |

## Input, sound, time, events

| MMBasic | Here | Ch. |
|---|---|---|
| `KEYDOWN(n)` | `keydown(n)` — identical, console-drain included | 21 |
| `ON KEY` | `keyboard.on_key(fn)` | 21 |
| `MOUSE(X)` etc. | `mouse("X")` etc. | 21 |
| `TOUCH(X)` etc. | `touch("X")` etc. — gestures latched | 21 |
| `PLAY TONE` | `tone(l, r, ms)` — retunes live, ends click-free | 20 |
| `PLAY SOUND v, ch, type, f` | `sound(voice, side, wave, freq, vol)` — same letters | 20 |
| `PLAY MODFILE` / `MODSAMPLE` | `play("f.mod", loop=True)` / `mod_sample(n)` | 20 |
| `PLAY WAV/MP3/FLAC` | `play(path)` — background, `wait=True` blocks | 20 |
| `PAUSE ms` | `time.sleep_ms(ms)` — callbacks still fire | 8, 21 |
| `TIMER` | `time.ticks_ms()` + `ticks_diff()` | 21 |
| `DATE$` / `TIME$` | `gettime()` tuple + f-strings | 28 |
| `SETTIME` | `settime(...)`; `ntpsync()` from the internet | 28, 29 |
| `SETTICK p, sub` / `SETTICK 0` | `machine.Timer(period=p, callback=f)` / `t.deinit()` | 32 |
| `SETPIN n, INTL/INTH/INTB` | `Pin(n).irq(f, Pin.IRQ_FALLING/RISING/both)` | 32 |
| `SETPIN DOUT/DIN/AIN/PWM` | `Pin` / `Pin(+PULL_UP)` / `machine.ADC` / `machine.PWM` | 31 |
| `WATCHDOG t` | `machine.WDT(timeout=ms)` + `.feed()` | 32 |
| `SYNC` | `pcgame.Clock` / `hdmi.vsync()` | 17, 22 |
| `OPTION CONSOLE` | `console("both"/"serial"/"screen"/"none")` | 2, 17 |
| `OPTION KEYBOARD` | `keymap("UK")` | 2 |
| `WEB NTP / GET / MQTT` (WebMite) | `ntpsync()` / `requests.get()` / `umqtt` | 29 |
| `MATH` verbs (CORREL, CHI, FFT, V_, Q_) | `pcmath` + `ulab.numpy` | 30 |
| `MM.INFO()` | `os.uname()`, `gc.mem_free()`, `os.statvfs()` | 34 |

Two things have no MMBasic ancestor and repay early study:
**dictionaries** (chapter 10) — lookup tables that replace every
parallel-array trick you own — and **`asyncio`** (chapter 32), a
cleaner shape for programs juggling several activities.
