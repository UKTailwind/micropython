# PLAN: AltGr fix + keyboard decoder unification

Status: **IMPLEMENTED 2026-08-13** (both phases, same day; board verification
still owed - see the checklists in sections 3 and 4). Deltas from the plan as
written:

- The seam gained a fifth function, `kbd_backend_msg(const char *)`, because
  the core inherited the orphaned-repeat guard (added to the Fuzix copy
  earlier the same day, after this plan was written) and its one diagnostic
  line needs a platform printer (kputs / mp_printf).
- `kbd_backend_on_key` takes the already-mapped code (an `int`), not
  (usage, mods): the core owns `kbd_map_code`, so the backend has no way -
  and no need - to map anything itself.
- Repeat defaults went to MMBasic's 600/150 in the core, as recommended:
  MicroPython moves from 400/40, Fuzix from 600/100.
- MP glue landed as `ports/rp2/kbd_backend.c` (in CMakeLists under
  `if(MICROPY_HW_USB_HOST)` next to the core, and in the emulator's
  micropython.mk); the sync check landed as `kbdsync.sh` in the Fuzix
  platform, relcheck.sh-style. All three vendored files hash-identical
  across the trees at implementation time.

Originally written 2026-08-13 after a user report:
Spanish keyboard + `kbd es` on Fuzix — AltGr combos type the wrong character or
nothing at all. Diagnosis confirmed the bug is layout-generic (DE/FR/ES/BE) and
present in **both** trees; PicoMite/MMBasic is unaffected.

## 1. The bug

The decoder has two translation paths and the AltGr logic is in the wrong one:

- `kbd_map_code()` — faithful port of MMBasic `APP_MapKeyToUsage`
  (PicoMite `input/KeyboardMap.c:937`), **including** the four AltGr specials
  tables. But its result only feeds the `KeyDown[]` table (`keyboard.keydown()`
  / future Fuzix ioctl) and the MicroPython `on_key` callback.
- `kbd_key()` — the function that pushes **typed bytes** — re-derives the
  character from `kbd_layout[usage*2 + shift]` inline and never checks
  `mods & 0x40`. The AltGr tables sit unused in the same file.

MMBasic has no such split: typing goes `process_key` → `APP_MapKeyToUsage` →
`USR_KEYBRD_ProcessData` (KeyboardMap.c:1175), so AltGr works on a PicoMite.
Classic silent divergence: the table was vendored verbatim, the plumbing wasn't.

Symptom detail (ES layout): AltGr+2 types `2` instead of `@`; the keys whose
plain column is 0 in `ESkeyValue` (usages 0x2F/0x31/0x34 → `[` `}` `{`) fall
through to the VT100 switch, match nothing, and type **nothing** — exactly the
brackets/braces needed to write C on Fuzix. Two combos are dead on real MMBasic
too and must stay silent: ES AltGr+E (€) and AltGr+4 (~), table code 0.

## 2. Duplication inventory (verified 2026-08-13)

Three consumers, one decoder:

| Consumer | Report source | Decoder copy |
|---|---|---|
| PC3 board MicroPython | `mp_usbh.c` (TinyUSB host) | `ports/rp2/kbd_decode.c` |
| PC emulator | `emulator/src/kbd_sdl.c` (SDL → synthetic HID reports) | same file, via `SRC_USERMOD_C` in emulator `micropython.mk` |
| PC3 Fuzix | `platform-rpipico/usbkbd.c` (TinyUSB host) | `platform-rpipico/kbd_decode.c` (vendored copy) |

Byte-identical today (checked with diff): `kbd_decode.h`, `keyboard_maps.h`.
Drifted: `kbd_decode.c` only. The drift:

1. **`kbd_push`** — MP: internal, handles `mp_interrupt_char` + stdin ringbuf.
   Fuzix: extern, ring in usbkbd.c; tty line discipline handles INTR.
2. **on_key** — MP has `kbd_notify_key` (mp_sched to a Python callback); Fuzix
   deleted it.
3. **Ticks** — MP `mp_hal_ticks_ms()`; Fuzix `time_us_64()/1000`.
4. **Auto-repeat engine** — Fuzix's is strictly better: configurable
   `kbd_repeat_first/next` (KBRATE ioctl, `kbd_set_repeat`) plus the
   starved-pump gap/settle logic (stops the phantom key-repeat after a
   full-screen repaint / boot-time starvation). MP still has the naive
   version with `#define` 400/40.
5. Includes/comments — cosmetic.

Repeat defaults disagree three ways: MP 400/40, Fuzix 600/100, **MMBasic
600/150** (verified: PicoMite `MM_Misc.c` treats `RepeatStart==600 &&
RepeatRate==150` as the don't-save default).

## 3. Phase 1 — fix AltGr (small, ships alone)

One edit, applied identically in both copies of `kbd_decode.c`: make
`kbd_key()`'s printing-key path call `kbd_map_code()` instead of its inline
lookup. This kills the intra-file duplication *and* is the bug fix — the AltGr,
Ctrl-letter, caps/shift and num-lock logic all come from the one function that
already matches MMBasic.

Order inside `kbd_key()` is unchanged: lock keys → num-lock-off keypad nav
redirect → F1–F12 xterm sequences → **printing path** → VT100 switch. The
printing path keeps its existing guard (`usage <= 0x64 && usage != 0x39 &&
!(0x49..0x52)`) and becomes:

```c
int v = kbd_map_code(usage, mods);
if (v > 0) {
    kbd_push((uint8_t)v);
    return;
}
// v == 0 (incl. dead AltGr combos): fall through to the sequence switch,
// which has no case for printing usages -> silence, as MMBasic.
```

Equivalence audit (why this is behavior-preserving apart from adding AltGr):

- **Ctrl-letter**: `kbd_map_code` returns `table[usage*2]-96` = 1..26; the old
  inline `v &= 0x1f` on either case column gives the same 1..26.
- **Letters**: both use `caps ^ shift` column selection. Non-letters: both use
  shift only.
- **Keypad**: digits with num-lock off are redirected to nav usages *before*
  this path, so `kbd_map_code`'s num-lock column pick only ever sees num-lock
  on (col 0 = digits) or keypad-5, whose row is `53, 53` in every layout —
  "kp5 keeps typing 5" survives. `/ * - + enter` (0x54–0x58) have equal
  columns.
- **Dead AltGr** (code 0): falls to the switch, which has no case for 0x08/0x21
  → nothing typed. Matches MMBasic's `c != 0` gate in `process_key`.
- **AltGr miss** (key not in the specials table): `kbd_map_code` falls through
  to normal translation, exactly as `APP_MapKeyToUsage` does.

Only Right-Alt (0x40) is AltGr, as MMBasic — do not add Ctrl+Alt synthesis.

### Phase 1 verification (side-by-side is the authority)

Real PicoMite with the same Spanish keyboard as reference, then:

- **Board MicroPython REPL** and **Fuzix console** (`kbd es`), full ES matrix:
  AltGr + `º 1 2 3 4 E ` + ´ ç` → `\ | @ # ~(dead) €(dead) [ ] { }` — i.e.
  usages 0x35/0x1E/0x1F/0x20/0x21/0x08/0x2F/0x30/0x31/0x34.
- DE spot-checks (AltGr+Q=@, AltGr+7/0={ }), FR (AltGr+0=@), BE.
- Regression: plain/shift/caps typing, Ctrl-C both environments, num-lock
  keypad both states, F-keys, auto-repeat still fires, `keyboard.keydown()`
  codes unchanged (that path already used `kbd_map_code`).
- Emulator: SDL right-Alt sets 0x40 in the synthetic report (`kbd_sdl.c`
  builds the report itself — verify the modifier byte first, then the same ES
  matrix).

## 4. Phase 2 — one byte-identical core, per-platform glue

Goal: `kbd_decode.c` becomes a self-contained, **byte-identical vendored file**
in both trees (like `kbd_decode.h` and `keyboard_maps.h` already are), with all
platform differences behind the backend seam. The FUZIX copy must stay
plain C with no MicroPython/pico-SDK includes (it is upstreamed to Alan Cox).

Backend seam (extend `kbd_decode.h`, keeping it identical in both trees):

```c
void kbd_push(uint8_t c);                       // already extern for Fuzix
void kbd_backend_set_leds(int slot, uint8_t leds); // already exists
uint32_t kbd_ticks_ms(void);                    // NEW - replaces mp_hal/pico time
void kbd_backend_on_key(uint8_t usage, uint8_t mods); // NEW - may be a no-op
```

Moves, per platform:

- **Core (`kbd_decode.c`, both trees)**: tables, `kbd_map_code`, `kbd_key`,
  `kbd_process_report`, `usb_kbd_keydown`, lock/LED bitmap, clear/stop, and the
  **Fuzix repeat engine** (configurable `kbd_repeat_first/next`,
  `kbd_set_repeat`, gap/settle logic — its hard-won comments come with it).
  Calls `kbd_backend_on_key()` where MP's `kbd_notify_key` calls sit today
  (new-press + each synthesised repeat). Drops `#include "py/*"`,
  `pico/time.h`, the `MICROPY_HW_USB_HOST` guard (see build note below).
- **MP glue** (new `ports/rp2/kbd_backend.c`, or fold into `mp_usbh.c`):
  `kbd_push` with the `mp_interrupt_char` check + `stdin_ringbuf`;
  `kbd_backend_on_key` = today's `kbd_notify_key` body (usbh_key_cb sched);
  `kbd_ticks_ms` = `mp_hal_ticks_ms`. Guarded by `MICROPY_HW_USB_HOST`;
  CMakeLists + emulator `micropython.mk` add the new file.
- **Emulator glue** (`kbd_sdl.c`): already provides `kbd_backend_set_leds`;
  shares the MP glue for push/on_key/ticks (it builds with
  `MICROPY_HW_USB_HOST=1` already).
- **Fuzix glue** (`usbkbd.c`): `kbd_push` + set_leds already there; add
  `kbd_ticks_ms` (time_us_64/1000) and a no-op `kbd_backend_on_key`.

Decisions to make when implementing:

1. **Repeat defaults**: recommend MMBasic's 600/150 in the core for both
   environments (replicate-MMBasic rule; Fuzix is already 600/100, MP's 400/40
   was never a considered choice). Note: changes MP typing feel slightly;
   KBRATE still adjusts Fuzix at runtime, and MP could grow
   `keyboard.repeat(first, next)` later for free since the vars exist.
2. **Sync mechanism**: the trees are separate repos with separate upstreams, so
   vendoring stays. Enforce byte-identity instead of hoping: a one-line hash
   check (à la `relcheck.sh`) comparing `kbd_decode.c/.h` +
   `keyboard_maps.h` across the two trees, run from the PC3 dev-notes
   checklist; both file headers state "byte-identical vendored copy — do not
   let these drift".

Build note: the core loses its `#if MICROPY_HW_USB_HOST` wrapper (Fuzix never
defines it; today's Fuzix copy already compiles unguarded). On the MP side the
file is only added to the build when USB host is on, which CMakeLists already
controls; verify a non-PC3 rp2 board still links.

### Phase 2 verification

- `diff` (or the new hash check) shows the two `kbd_decode.c` byte-identical.
- Re-run the Phase 1 matrix on **board MP + Fuzix + emulator** (all three
  consumers now share the exact code — one pass each).
- Fuzix: KBRATE/`kbdrate` still works; boot-time "phantom Return after hdb2"
  stays fixed (the settle logic survived the move).
- MP: `keyboard.on_key` callback still fires on press and on auto-repeat;
  Ctrl-C interrupt still raises KeyboardInterrupt from the keyboard.
- Repeat-default change (if 600/150 adopted) called out in the release notes.

## 5. Sequencing

Per the one-change-at-a-time rule: Phase 1 lands and is **board-verified in
both environments** before Phase 2 starts; Phase 2 is a pure refactor + repeat
convergence with no other behavior changes riding along. Phase 1 is ~10 lines
per copy; Phase 2 is a ~150-line move plus glue and build wiring.

Out of scope, noted for later: dead-key accents (´ ` ¨ compose) and non-ASCII
(€ ñ) — MMBasic doesn't do them either, and matching MMBasic is the spec;
`keyboard.repeat()` Python API; Fuzix KEYDOWN ioctl using `usb_kbd_keydown`.
