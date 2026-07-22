# Chapter 16 — Bitmap images and sprite sheets

Chapter 15 drew from geometry; this chapter draws from **files**.
Photographs, pixel art, game sprites — pictures made elsewhere (or
saved earlier) and summoned to the screen. It comes in two halves
again: *showing* pictures, which turns the machine into a photo frame,
and *holding* pictures in memory to stamp at will — the sprite sheet,
which is the raw material of every game in Part IV.

## Putting a picture on the screen

Three decoders, one per format, all injected and ready:

```python
draw_jpg("/sd/holiday.jpg")          # JPEG -- photos
draw_bmp("/sd/logo.bmp")             # BMP  -- simple, huge, dependable
draw_png("/sd/ghost.png")            # PNG  -- artwork, with transparency
```

Each takes `x=`, `y=` to place the top-left corner (default: the
screen's), and draws straight onto the framebuffer — over whatever was
there, console text included. `draw_jpg` also takes `scale=2` (or 4,
or 8) to shrink a big photo on its way in: a 2560-pixel-wide camera
file lands as 640 at `scale=4`, which is exactly the trick the
slideshow below uses.

Get pictures aboard the chapter 4 way: fill an SD card from a PC. (Or
the chapter 5 ways — XMODEM works on binary files too.)

## The colour squeeze, and dithering

Show a photograph plainly in the default mode and it will look...
*posterised* — smooth skies collapsing into bands. No bug: chapter 15
told you the deal — `RGB640` has 256 colours, and a sunset has more.

The classic cure is **dithering**: trading colour depth for texture by
speckling two nearby colours so the eye averages them. The decoders do
it on request:

```python
draw_jpg("/sd/holiday.jpg", dither=True)
```

`dither=True` (Atkinson dithering — the same algorithm that made the
original Macintosh's one-bit photos famous) is the setting to
remember. It transforms photos in the 256-colour and 16-colour modes,
and does nothing in the 65,536-colour modes, which don't need it. And
that leads to this machine's best photographic surprise:

```python
screen(hdmi.RGB1024)                          # 1024x600... 16 colours?!
draw_jpg("/sd/holiday.jpg", dither=True)
```

Full-resolution, sixteen colours, Atkinson-dithered — and genuinely
lovely, in a woodcut-print way. Try it before you doubt it. (The
User Manual, section 10, has the connoisseur's notes on the other
dither settings; `True` is the right default.)

The other direction still works, of course — `save_image("art.bmp")`
from chapter 15 writes the screen *to* a file, so anything you draw
can be reloaded, traded, or shown on a PC. Keep it in mind as a
screenshot habit, too: it captures evidence of high scores.

> **Coming from MMBasic:** `LOAD JPG`/`LOAD PNG`/`LOAD BMP` became
> `draw_jpg`/`draw_png`/`draw_bmp`, and `SAVE IMAGE` is `save_image` —
> same jobs, same results, dithering included.

## Transparency, part one: PNG alpha

Artwork is rarely rectangular — a ghost sprite is ghost-shaped, and
the PNG format records which pixels are *see-through* (its "alpha"
channel). `draw_png` honours it: transparent pixels simply aren't
drawn, and the background shows through. Draw your landscape, then
`draw_png("/sd/ghost.png", x=300, y=150)` — the ghost floats over the
scene, no rectangle in sight. (The `cutoff=` argument sets how opaque
a pixel must be to count; the default is sensible.)

That covers artwork *arriving* with transparency. Games need
something more general — next section.

## Sprite sheets: pictures held in memory

Drawing from the SD card takes a moment — fine for a photo, hopeless
for a spaceship needed sixty times a second. Games keep their artwork
*decoded, in RAM*, and stamp from there. And they pack it as a
**sprite sheet**: one image holding a grid of cells — every frame of
every character, side by side. One file to load, one buffer in
memory, and any cell stampable by grid position:

```python
d = hdmi.fb()
sheet = load_image("/sd/invaders.png", transparent=d.colour(MAGENTA))

sheet.cell(0, 0, 16, 16, 100, 80, skip=d.colour(MAGENTA))   # col 0, row 0
sheet.cell(3, 1, 16, 16, 200, 80, skip=d.colour(MAGENTA))   # col 3, row 1
```

`load_image(path)` decodes into an **`Image`** object (`img.w` and
`img.h` tell you its size; `img.blit(x, y)` stamps the whole thing).
`img.cell(col, row, cw, ch, x, y)` treats it as a grid of `cw × ch`
cells and stamps one — *col 3, row 1* of a 16×16 grid, no pixel
arithmetic required.

And the **skip colour** is transparency, part two — the games
version. Any source pixel matching `skip` is simply not copied, so the
magenta background around each invader stays behind and the invader
lands cut out. Why magenta? Pure convention: `0xFF00FF` is a colour no
sane artwork uses, which makes it the industry's masking tape. The
`transparent=` argument at load time pre-fills the buffer with it, so
a PNG's transparent regions *become* magenta, ready to skip. One
colour, two jobs, zero effort.

![A sprite sheet is one image holding a grid of cells; `img.cell(col, row, …)` stamps one. The **skip colour** (magenta) is never copied, so each sprite lands cut out over whatever is already on screen.](figs/16-sprite-sheet.png)

Two housekeeping notes from the manual: the decoded buffer is in the
current mode's pixel format, so **reload your images after a
`screen()` change** (the chapter 15 rule, extended); and in `RGB1024`
keep cell widths and x-positions even.

## `hdmi.blit`: the universal rectangle mover

`cell` and `img.blit` ride on something more general, worth knowing by
name because chapters 17 and 18 live on it. `hdmi.blit(...)` copies
any rectangle — screen to screen, memory to screen, screen to memory:

```python
# copy a 100x80 patch of the screen 200px right
hdmi.blit(50, 50, 100, 80, 250, 50)

# shift a whole strip 4px left -- instant horizontal scrolling
hdmi.blit(4, 100, hdmi.width() - 4, 60, 0, 100)
```

The second line is the one to sit with: shifting a region over itself
is legal and fast, and it is how marquees, scrolling landscapes and
chapter 19's camera all move. Blits also take the `skip=` colour, and
can be clipped off-screen safely. (The full signature, including
copying between the screen and the off-screen buffers we meet next
chapter, is in the manual — section 5.)

> **Coming from MMBasic:** this *is* `BLIT`, feature for feature —
> skip colour, overlapping copies and all. `load_image` plays the role
> of `BLIT READ`'s memory frames, with the sheet cut into cells for
> you.

## An asset pipeline with no PC

Here is a satisfying loop-closer: the machine can manufacture its own
sprite sheets. Draw cells with chapter 15's tools, save, reload:

```python
d = hdmi.fb()
MASK = d.colour(MAGENTA)
d.fill(MASK)                                   # transparent-to-be

for i in range(4):                             # four 32x32 cells in a row
    x = i * 32
    d.fill_rect(x + 4, 10, 24, 18, d.colour(GOLD))       # body
    d.fill_rect(x + 8, 4, 16, 8, d.colour(LITEGRAY))     # head
    d.fill_rect(x + 6, 28, 4, 4 - i % 2 * 2, d.colour(RED))   # left leg
    d.fill_rect(x + 22, 28, 4, 2 + i % 2 * 2, d.colour(RED))  # right leg

save_image("robot.bmp")
```

Four robots, legs alternating — a walk cycle, drawn by loop. Now load
the sheet back and animate it, treating your own drawing exactly as you
would a sprite sheet bought from an artist:

```python
import time

d = hdmi.fb()
MASK = d.colour(MAGENTA)
bot = load_image("robot.bmp", transparent=MASK)

d.fill(d.colour(0x102040))
for step in range(60):
    frame = step % 4
    bot.cell(frame, 0, 32, 32, 40 + step * 8, 200, skip=MASK)
    time.sleep(0.1)
```

It *walks* — leaving a trail of itself, admittedly, because nothing
erases the old frames yet. That smear is not a bug to fix today; it is
chapter 17's opening scene. What matters tonight: you drew, saved,
loaded, cut and animated a sprite sheet without a PC in the room.

## Project: the photo frame

The machine earns its place on a shelf. `edit("frame.py")`:

```python
import os
import time
import random

DELAY = 8                                # seconds per photo

def photo_list():
    try:
        files = os.listdir("/sd")
    except OSError:
        return []
    return sorted(f for f in files if f.lower().endswith((".jpg", ".jpeg")))

d = hdmi.fb()

while True:
    photos = photo_list()
    if not photos:
        d.fill(d.colour(BLACK))
        hdmi.text("no photos -- insert SD card", 40, 40, d.colour(GRAY))
        time.sleep(3)
        continue
    pick = random.choice(photos)
    try:
        draw_jpg("/sd/" + pick, dither=True)
    except OSError:
        continue                         # card pulled mid-read: just retry
    caption = pick.lower().replace(".jpeg", "").replace(".jpg", "")
    hdmi.text(caption, 8, hdmi.height() - 16, d.colour(LITEGRAY), d.colour(BLACK))
    time.sleep(DELAY)
```

Read it as a chapter-13 graduate: the SD card can vanish at any moment
(it lives in someone's hallway now), so *every* touch of `/sd` is
guarded, and an empty card is a message, not a crash. The parts you
built earlier are all on shift — `endswith` with a tuple filters,
`replace` cleans the caption, `random.choice` picks, and the caption's
solid `bg=` is the eraser trick in disguise. Install it as `/main.py`
(chapter 5) and the machine boots straight into the family album;
run it in `RGB1024` for the gallery-print look.

## Experiments

1. The dither taste test: one photo, drawn three ways — `dither=False`,
   `dither=True`, and mode `RGB1024` with `dither=True`. Same file,
   three eras of computing.
2. A contact sheet: `draw_jpg(f, x, y, scale=8)` tiles camera photos
   at thumbnail size — two loops (chapter 9's arrangement/element!)
   over a 4×3 grid, filenames in font 8 beneath each.
3. Marquee: `save_image` your chapter 15 poster, `load_image` it, and
   use the shift-over-itself blit to slide it across the screen. Then
   try shifting *up*.
4. Proto-sprite: use the manual's save-and-restore blit pair (grab the
   patch under the robot into memory, stamp robot, restore patch, move
   on) to make the walking robot *stop smearing*. You are twenty lines
   from reinventing chapter 18.
5. In `frame.py`, honour a caption file: if `beach.jpg` has a
   `beach.txt` beside it, show that text instead of the filename
   (chapter 12; `try`/`except` for when it doesn't).

## Challenges

1. **The gallery.** `frame.py` grown up: sorted order with a
   remembered position (a one-line file, chapter 12), a title card at
   boot, and a "no repeats until all shown" shuffle (chapter 10's
   `remove` trick).
2. **Sheet forge.** Improve the robot sheet: four *distinct* walk
   frames (arms too), a second row of jump frames, 22 named colours at
   your disposal — then publish: `cp` the BMP to `/sd`, and write the
   loader another program can import (`robots.py` with a `walk(x, y,
   step)` function — chapter 11 says you know what to do).
3. **Ken Burns, budget edition.** One big photo, drawn once — then a
   slow pan: blit a screen-sized *region* of it, moving the source
   rectangle a pixel per tick. (Load the photo to memory first;
   `img.blit`'s `sx`/`sy` arguments are the viewport.)
4. **The photo booth.** Combine chapters: draw a frame and caption
   *around* a photo (15), stamp the walking robot in the corner (16),
   show the date from `gettime()` (12), and `save_image` the lot as a
   postcard file for the SD card. One program, five chapters, one
   keepsake.
