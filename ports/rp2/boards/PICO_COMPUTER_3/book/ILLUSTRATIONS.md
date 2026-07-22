# Illustration plan — where diagrams would help the reader

A review of all 35 chapters + appendices for places a figure would carry
more than prose can. Today the book has exactly one image (the board photo
in ch1); everything else is text and code. Many concepts here are inherently
**spatial** (coordinates, buffers, tile grids, memory maps, circuits,
vectors) or **temporal** (the vsync race, the game loop, interrupts) — the
cases where a diagram is worth a page of careful words.

## Two kinds of figure (plan them differently)

1. **Machine-made screenshots** — the book already draws-and-`save_image`s
   posters, tilesets, sprite sheets. The board can *produce its own figures*
   (`save_image("x.bmp")` → convert to PNG on a PC), which is accurate,
   free, and true to the book's "no PC needed" ethos. Prefer these wherever
   the figure is "what the screen shows".
2. **Conceptual line diagrams** — coordinate axes, memory maps, the game
   loop, pinouts, vectors, state machines. These need drawing (SVG →
   embedded PNG, matching the cover's clean style). This is the real work.

Mechanics: figures are `![caption](figs/NN-name.png)`; a `figs/` folder and
a Makefile prerequisite mirror the existing `cover.png` wiring. Keep a single
visual language (same stroke weight, palette, and the board's named colours).

Priority key: **P1** = high impact, concept is hard without it · **P2** =
clearly helpful · **P3** = nice-to-have polish.

---

## Tier 1 — the chapters that most need figures

### Ch 17 — Smooth motion (buffers & overlay)  ★ the strongest case
Almost every idea here is spatial/temporal and currently prose-only.
- **P1 The vsync race** (§Stage 1): a timeline — 60 Hz scanout vs your
  erase→draw, showing the scanout catching the gap. Explains the flicker in
  one picture.
- **P1 Double buffering** (§Stage 3): F (hidden) ← draw; then `vsync`+`copy`
  → N (visible). The "workbench the monitor can't see".
- **P1 The N / L / F buffers**: one diagram naming the three surfaces
  (visible screen N, acetate layer L, off-screen F) — anchors ch16–19 & 34.
- **P2 The overlay/layer** (§The overlay): acetate over scenery,
  black = see-through, merged per scanline.
- **P2 Video-RAM split** (§Why only 320×240): two 320×240 screens fill video
  RAM; the layer *is* the second half. (Shared with ch34's figure.)

### Ch 34 — Performance & memory
- **P1 Where the frame lives** (§Where the frame lives): the video-RAM
  block, showing what fits on-chip per mode — RGB640_4 (frame + F),
  RGB320 (frame + F *or* layer), RGB320_8 (N + L + F all on-chip). This is
  pure spatial allocation, very hard to follow in words; the payoff diagram
  of the book.
- **P1 SRAM vs PSRAM**: 512 KB fast on-chip vs 8 MB slow external, the "bus
  toll" a double-buffered frame pays each frame.
- **P2 The GC sawtooth**: free memory over time (allocate → collect), and
  fragmentation as free-but-unusable gaps.
- **P3 The optimisation ladder**: the 5 rungs as a visual ladder.

### Ch 19 — Tile maps  ★ (the reader's own example)
- **P1 Tileset → indices → picture**: the tile strip (1..6, 0 = empty) + a
  small index grid + the rendered result. The core mental model.
- **P1 The three coordinate systems**: cell (5,3) → world-pixel (80,48) →
  screen (minus camera). The chapter itself calls this "half of tile-map
  programming" — exactly what a diagram fixes.
- **P2 Camera / viewport**: the big world with the screen-sized viewport
  window, `vx,vy` and the clamp limits (world − screen).
- **P2 Axis-by-axis collision**: the player rect vs solid tiles, sliding
  along a wall (the try-x-then-y idiom).

### Ch 15 — The screen up close
- **P1 Coordinate system**: (0,0) top-left, x→, y↓, corners (639,479),
  centre (320,240). The foundation for all of Part III.
- **P2 RGB / hex colour**: additive R+G+B light and the `0xRRGGBB` byte
  layout.
- **P2 The framebuffer**: a grid of numbered cells → the monitor (core 1
  shipping it 60×/s).
- **P3 `arc()` angles**: 0° = up, clockwise, `a1`/`a2` sweep.

### Ch 31 — GPIO / talking to the world
Electronics without pictures is the hardest ask in the book.
- **P1 The I/O-header pinout** (§The header map): labelled pin map (shared
  with Appendix D).
- **P1 Wiring diagrams**: LED + resistor, button + pull-up (and "why pins
  float"), pot → ADC, QWIIC/I2C module. Circuits *need* schematics.
- **P2 PWM duty cycle**: square waves at 10/50/90 % → brightness/speed.

---

## Tier 2 — clearly helpful

### Ch 22 — Anatomy of a game
- **P1 The game loop**: read input → update world → draw → repeat (the
  canonical cycle).
- **P2 Game-state machine**: title → playing → game-over transitions.
- **P3 Fixed-timestep metronome**: frames pinned to vsync.

### Ch 24 — Asteroids (vectors & rotation)
- **P1 Heading & thrust vector**: angle θ, `vx = cos θ`, `vy = sin θ`, the
  thrust adding to velocity. Trig is inherently diagrammatic.
- **P2 Toroidal wrap**: leave the right edge, enter the left ("space wraps").

### Ch 16 — Images & sprite sheets
- **P1 Sprite-sheet grid**: the sheet with a cell grid; `cell(col,row)`
  picking col 3/row 1.
- **P2 Skip-colour masking**: magenta box → cut-out sprite (before/after).
- **P2 `blit` / shift-over-itself**: source rect → dest rect, and the
  self-overlapping shift that scrolls.
- **P3 Dithering**: posterised bands vs Atkinson-dithered (a screenshot pair).

### Ch 18 — Sprites
- **P1 AABB collision**: two bounding boxes, overlap test.
- **P2 `grab` from the sheet**; **sprite layers / z-order**.

### Ch 10 — Collections
- **P1 Index & slice**: the boxes-with-indices figure (positive & negative
  indices; slice boundaries *between* elements). The classic clarifier.
- **P3 Dict as key→value** mapping.

### Ch 09 — Turtle graphics
- **P1 The exterior-angle insight**: a polygon with the 360/n turn marked
  (why the square turns 90°, the pentagon 72°); the star's overshoot angle.
  Angles are pure geometry — a figure teaches faster than any sentence.

### Ch 35 — The system itself
- **P1 Boot sequence**: power → bootrom → `_boot` → `_boot_board` →
  `main.py` → REPL (§The five seconds after the switch).
- **P1 Flash geography** (§The geography): the flash map — firmware region
  vs 12 MB filesystem (turn the existing table into a to-scale bar).

### Ch 27 — GUI toolkit
- **P1 Describe-then-poll model**: widgets declared → event loop polls.
- **P2 Layout grid**: the calculator's button grid / settings-screen layout.

---

## Tier 3 — polish where a small figure lifts a page

- **Ch 01** — labelled board callout (beyond the photo) + a one-box
  system block diagram (CPU · HDMI · USB host · SD · audio · RTC · Wi-Fi).
- **Ch 02** — a "plug it in here" wiring picture (HDMI, USB, power, SD) and
  a flashing flowchart (HUB switch → Prog → BOOT+RESET → drop `.uf2`).
- **Ch 13** — anatomy of a traceback (labelled call-stack + arrow) and the
  try/except/finally flow.
- **Ch 14** — class-as-blueprint → many instances (each with its own
  attributes).
- **Ch 08** — while vs for flow (small flowcharts), break/continue.
- **Ch 11** — function call: arguments in → return out; scope as a "sealed
  room".
- **Ch 20** — waveform (frequency = pitch, amplitude = volume); the four
  voices as parallel channels.
- **Ch 28** — the 8-field time tuple, labelled; the three clocks (RTC /
  system / NTP) and how they sync.
- **Ch 29** — request→response (device → HTTPS → JSON back); MQTT pub/sub
  through a broker.
- **Ch 33** — an interrupt (main flow → handler → resume); asyncio tasks
  yielding on a timeline.
- **Ch 25** — the dialogue state machine.
- **Ch 30** — array-vs-loop (one C op over a row vs a Python loop).

### Appendices
- **D (Hardware reference)** — the definitive **full pinout** and a
  labelled **board diagram**; the screen-modes table as a visual
  resolution/colour comparison. Reference figures the chapters can point to.
- **A (Cheat sheet)** — tiny inline glyphs: the coordinate axes, the colour
  byte, the buffer trio.

---

## Suggested sequencing

1. **Reusable anchors first** (used by many chapters): the N/L/F buffer trio
   (17), video-RAM split (34/17), coordinate system (15), the pinout (31/D).
2. **The reader's flagged pain points**: tile maps (19) and buffers (17).
3. Then Tier 2 chapter-by-chapter, then Tier 3 polish.

Roughly ~30 conceptual diagrams + ~10 machine-made screenshots covers the
book; the Tier-1 set (~14 figures) delivers most of the reader benefit.
