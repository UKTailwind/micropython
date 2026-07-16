# The Pico Computer 3 book — source

This directory holds the Markdown source of **Programming the Pico Computer 3**,
the beginner-to-advanced course book. The chapter plan is in
[OUTLINE.md](OUTLINE.md); the firmware reference it defers to is
[../USER_MANUAL.md](../USER_MANUAL.md).

## Layout

- `NN-slug.md` — one file per chapter, numbered `01`–`34`. The number prefix
  fixes the build order.
- `appendix-X-slug.md` — appendices A–H, built after the chapters.
- `metadata.yaml` — title page / Pandoc metadata shared by every output.
- `Makefile` — builds the finished formats with Pandoc.

## Building

Requires [Pandoc](https://pandoc.org) (and TeX Live for the PDF). In WSL Ubuntu:

```bash
sudo apt install pandoc                      # HTML / EPUB / docx
sudo apt install texlive-xetex texlive-latex-recommended texlive-latex-extra fonts-dejavu   # PDF too
```

The PDF uses XeLaTeX + DejaVu fonts so the arrows, `π` and other Unicode in
the chapters typeset correctly.

Then:

```bash
make html    # pico-computer-3-book.html  (single self-contained file)
make epub    # pico-computer-3-book.epub
make pdf     # pico-computer-3-book.pdf
make docx    # pico-computer-3-book.docx  (export for reviewers)
make         # html + epub + pdf
make clean
```

The Markdown itself renders fine on GitHub, so the book is readable without
building anything. Plain-text chapters can also be copied to an SD card and
read on the Pico Computer itself with `cat()` or `fm()`.

## Writing conventions

- Each chapter is built around a project and ends with **Experiments**
  (guided tweaks) then **Challenges** (open-ended; hints live in appendix F).
- Chapters carrying a big new concept (10 collections, 11 functions,
  14 classes, 22 the game loop) get the full preview/teach/recap
  treatment: a roadmap up front, an italic *"In one line: …"* after each
  section, and a "What you now hold" summary before the exercises.
- MMBasic crossover notes are blockquotes starting `> **Coming from
  MMBasic:**` — keep them short, one concept each.
- Code blocks are fenced with `python`. Anything shown at the prompt uses the
  `>>>` prefix only when the *response* matters; program listings have no
  prefix so readers can paste them via `autosave()`.
- Every code sample must run on the current firmware exactly as printed —
  test on the board (or against `../tests/`) before committing.
- **Every fenced block is a complete, pasteable program** (readers feed
  them to `autosave()`): no fragments that rely on an earlier listing's
  variables, no dependence on files the reader may not have (draw or
  generate assets in the listing itself). Repetition between stages of a
  worked example is a feature.
- Any program that redirects drawing (`hdmi.write("F")`/`"L"`) wears the
  `try:`/`finally: hdmi.write("N")` collar, so Ctrl-C can never strand
  the console on an invisible buffer. Full-screen animations also pair
  `console("none")` (kills the blinking console cursor; firmware v0.8+)
  with `console()` in the same `finally`.
- Programs that `hdmi.create()` or `hdmi.layer()` first `hdmi.close()`
  the same target ("start clean — rerun-proof"): create/layer raise
  `ValueError` if the target already exists, and a previous run's
  Ctrl-C leaves it existing. `close()` is safe when nothing's there.
- Buffer-order rule: choose the write target, *then* take `hdmi.fb()` —
  the Display binds to the target current at the moment of the call.
- Full-screen programs **declare their screen mode** (`screen(hdmi.RGB640)`
  + `time.sleep(3)`) rather than inheriting whatever mode was persisted —
  the saved mode is whatever the user last chose, not what the layout
  assumes.
- API depth: teach the everyday form and link the User Manual for the rest.
  Don't duplicate reference tables that live in the manual.
- `TODO(hw)` marks facts about the physical board (photos, connector
  positions) that need checking against real hardware.
