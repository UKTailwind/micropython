# Appendix G — Glossary

Plain English, one breath each.

| **argument** — a value handed to a function when calling it.
| **array (ulab)** — a list that does maths on all its elements at once, in C.
| **assignment** — `=`: *becomes*. Attaches a name to a value.
| **attribute** — a variable living on an object: `hero.health`.
| **block** — the indented lines belonging to an `if`, loop, or `def`.
| **boolean** — `True` or `False`; what comparisons manufacture.
| **bug** — the program doing exactly what you *said*, not what you meant.
| **bytes** — raw 8-bit data (`b"..."`); what networks and files really move.
| **callback** — a function you hand over to be called when something happens.
| **class** — a blueprint for making objects: data plus behaviour, one parcel.
| **clamp** — forcing a value to stay inside limits: `max(lo, min(hi, x))`.
| **collection** — one variable holding many values: list, tuple, dict.
| **comment** — `#`: a note for humans; the machine skips it.
| **comprehension** — building a list in one expression: `[x for x in xs if ...]`.
| **console** — where typed commands and printed text meet: screen and/or serial.
| **dictionary** — a collection that files values under keys, not positions.
| **dt** — the seconds since last frame; multiply speeds by it (ch. 22).
| **edge (vs level)** — the moment of change, not the state: "just pressed".
| **exception** — an alarm raised where trouble happens, catchable by name.
| **expression** — anything that produces a value: `2 + 2`, `f(x)`, `a if c else b`.
| **f-string** — `f"..."`: text with live values woven in at `{}` marks.
| **flag** — a variable that remembers whether something has happened.
| **float** — a decimal-point number; fast, and slightly approximate.
| **framebuffer** — the grid of pixel values in memory that *is* the screen.
| **frozen module** — a library baked read-only into the firmware (`.frozen`).
| **function** — a named, reusable block with its own parameters and result.
| **garbage collector** — reclaims memory from objects nothing refers to.
| **GPIO** — a general-purpose pin: your programs touching real electrons.
| **handler** — a callback for an interrupt, key, or GUI event.
| **heap** — the memory where all Python objects live (here: the 8 MB PSRAM).
| **hex** — base-16 numbers, `0x1F`; two digits per byte, ideal for colours.
| **I2C** — a two-wire bus where each device answers at an address.
| **immutable** — cannot be changed after creation: strings, tuples.
| **index** — a position in a sequence, counted from 0 ("steps from the front").
| **infinite loop** — `while True:`; a feature when you own the exit.
| **interrupt** — hardware tapping your program on the shoulder between statements.
| **iteration** — one pass of a loop; iterating = visiting each item.
| **JSON** — the internet's data format; dicts and lists wearing quotes.
| **key** — what a dictionary files a value under.
| **lambda** — a tiny nameless function written in one expression.
| **library / module** — a `.py` file (or frozen equivalent) served by `import`.
| **list** — an ordered, changeable sequence: `[1, 2, 3]`.
| **local variable** — born inside a function, dies when it returns.
| **loop** — machinery for repetition: `for`, `while`.
| **method** — a function living on an object, called through the dot.
| **mutable** — changeable in place: lists, dicts, your objects.
| **None** — the value meaning "no value"; what a bare `return` returns.
| **object** — a parcel of data plus behaviour, stamped from a class.
| **parameter** — a function's named slot that an argument fills.
| **path** — a file's full address: `/sd/games/breakout.py`.
| **pixel** — one dot of the screen; one cell of the framebuffer.
| **polling** — asking repeatedly ("is it pressed *now*?") instead of waiting.
| **PWM** — switching a pin very fast so it averages to in-between values.
| **REPL** — the `>>>` prompt: Read, Evaluate, Print, Loop.
| **return value** — what a function hands back to its caller.
| **scope** — where a name is visible; functions are sealed rooms.
| **sentinel** — a special value meaning "nothing here": `-1`, `None`.
| **sequence** — anything with ordered items: strings, lists, tuples, ranges.
| **slice** — a piece of a sequence: `s[2:5]` — start included, stop not.
| **sprite** — a movable image the engine draws and erases without harm.
| **state machine** — one `state` variable choosing what the loop means now.
| **string** — text: `"like this"`.
| **syntax** — the grammar; `SyntaxError` = the sentence didn't parse.
| **task (asyncio)** — a function juggled cooperatively with others; pauses at `await`.
| **traceback** — the obituary of an uncaught exception; read bottom-up.
| **tuple** — a fixed bundle of values that travel together: `(x, y)`.
| **type** — a value's kind: `int`, `str`, `Hero`...
| **variable** — a name with a value attached; the machine's memory of your making.
| **viewport** — the window a camera shows of a larger world (ch. 19).
| **vsync** — the between-frames instant when drawing can't be caught mid-change.
| **watchdog** — a timer that reboots the machine unless regularly fed.
| **wildcard** — `*`/`?` in a filename pattern: `rm("*.tmp")`.
