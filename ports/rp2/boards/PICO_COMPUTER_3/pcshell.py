# Shell-style helpers for the Pico Computer 3. Injected into the REPL namespace
# by _boot.py so they can be used without an explicit import.

import os
import sys
import time


def _glob_match(name, pat):
    # Case-insensitive filename glob: * (any run) and ? (one char). Iterative,
    # so no recursion depth worries.
    name = name.lower()
    pat = pat.lower()
    n = p = 0
    star = -1
    mark = 0
    ln, lp = len(name), len(pat)
    while n < ln:
        if p < lp and (pat[p] == "?" or pat[p] == name[n]):
            n += 1
            p += 1
        elif p < lp and pat[p] == "*":
            star = p
            mark = n
            p += 1
        elif star >= 0:
            p = star + 1
            mark += 1
            n = mark
        else:
            return False
    while p < lp and pat[p] == "*":
        p += 1
    return p == lp


def _split_pattern(path):
    # Split "dir/pattern" into (directory, pattern-or-None); a wildcard is only
    # honoured in the last path component (single-level glob, like MMBasic).
    if "*" not in path and "?" not in path:
        return path, None
    slash = path.rfind("/")
    if slash < 0:
        return os.getcwd(), path
    return (path[:slash] or "/"), path[slash + 1:]


def ls(path=None):
    """List a directory (dirs first, then files, alphabetically) with size and
    modification date/time. Defaults to the current directory. A wildcard in the
    last component filters, e.g. ls("/sd/mp3/*.mp3") or ls("*.py")."""
    if path is None:
        path = os.getcwd()
    directory, pattern = _split_pattern(path)
    sep = "" if directory.endswith("/") else "/"
    rows = []
    for entry in os.ilistdir(directory):
        name = entry[0]
        if pattern is not None and not _glob_match(name, pattern):
            continue
        try:
            st = os.stat(directory + sep + name)
            isdir = bool(st[0] & 0x4000)
            size, mtime = st[6], st[8]
        except OSError:
            isdir, size, mtime = False, 0, 0
        # Sort key: dirs (not isdir == False) before files, then name A-Z.
        rows.append((not isdir, name.lower(), name, isdir, size, mtime))
    rows.sort()
    for _, _, name, isdir, size, mtime in rows:
        t = time.localtime(mtime)
        stamp = "%04d-%02d-%02d %02d:%02d" % (t[0], t[1], t[2], t[3], t[4])
        print("%10s  %s  %s" % ("<dir>" if isdir else size, stamp, name))
    print("%d item%s" % (len(rows), "" if len(rows) == 1 else "s"))


def run(path, *args):
    """Launch a Python program from a file.

    Runs with __name__ == "__main__" (so `if __name__ == "__main__":` blocks
    fire) and with the working directory temporarily set to the program's
    folder, restored afterwards. The program's namespace is seeded with a COPY
    of the REPL globals, so the injected helpers (touch, ls, play, hdmi, the
    colour palette, ...) are available exactly as at the prompt — but since it's
    a copy, the program can't clobber the real REPL globals.

    Extra arguments become the program's command line, as on desktop Python
    (and as MMBasic's RUN "prog", cmdline / MM.CMDLINE$):

        run("convert.py", "in.wav", "out.flac")

    The program sees sys.argv == ["convert.py", "in.wav", "out.flac"] —
    argv[0] is the program path, the rest arrive as strings. sys.argv is
    restored when the program ends."""
    import __main__
    import sys

    slash = path.rfind("/")
    folder = path[:slash] if slash > 0 else ("/" if slash == 0 else None)
    with open(path) as f:
        # compile with the real filename so tracebacks say `File "prog.py",
        # line N` instead of `File "<string>"` -- run/edit/fix needs the name
        code = compile(f.read(), path, "exec")
    g = {k: v for k, v in __main__.__dict__.items() if not k.startswith("__")}
    g["__name__"] = "__main__"
    g["__file__"] = path
    cwd = os.getcwd()
    # sys.argv is a fixed list object on builtin modules: swap its CONTENTS
    argv_saved = list(sys.argv)
    try:
        if folder is not None:
            os.chdir(folder)
        sys.argv[:] = [path] + [str(a) for a in args]
        exec(code, g)
    finally:
        sys.argv[:] = argv_saved
        os.chdir(cwd)
        # If the program died (or was Ctrl-C'd) while hdmi.write("F"/"L") was
        # selected, console output would keep going to the invisible buffer and
        # the REPL would LOOK dead. Always come home to the display, as MMBasic
        # does when a program ends.
        try:
            import hdmi
            hdmi.write("N")
        except Exception:
            pass


def edit(*args, **kwargs):
    """Launch the pye full-screen editor. edit("/sd/foo.py") opens (or creates)
    a file; Ctrl-S saves, Ctrl-Q quits. pye is imported lazily so it costs no
    RAM until first use."""
    import pye

    return pye.pye(*args, **kwargs)


def cls():
    """Clear the console screen and home the cursor (MMBasic's CLS)."""
    sys.stdout.write("\x1b[H\x1b[2J")


def pwd():
    """Print the current working directory."""
    print(os.getcwd())


def cd(path="/"):
    """Change the current working directory (defaults to root)."""
    os.chdir(path)


def mkdir(path):
    """Create a directory."""
    os.mkdir(path)


def rmdir(path):
    """Remove an empty directory."""
    os.rmdir(path)


def rm(path):
    """Remove a file. A wildcard in the last path component removes every
    matching file, e.g. rm("*.tmp") or rm("/sd/log/*.log")."""
    for p in _expand(path):
        os.remove(p)


def _console_size():
    # (cols, rows) of the on-screen console. Prefer the live console geometry;
    # fall back to the 640x480 8x12 default grid if it isn't running.
    try:
        import pcconsole

        con = pcconsole._con
        if con is not None:
            return con.cols, con.rows
    except Exception:
        pass
    try:
        import hdmi

        return hdmi.width() // 8, hdmi.height() // 12
    except Exception:
        return 80, 40


def _getkey():
    # Blocking single-key read, mirroring pye's IO_DEVICE. Disable the Ctrl-C
    # interrupt char so it arrives as a normal byte, read raw, then restore.
    # The loop past empty reads means a non-blocking stdin still waits for a
    # key instead of returning "" (which would let cat run straight through).
    try:
        from micropython import kbd_intr
    except ImportError:
        kbd_intr = None
    if kbd_intr:
        kbd_intr(-1)
    try:
        rd = sys.stdin.buffer.read if hasattr(sys.stdin, "buffer") else sys.stdin.read
        while True:
            c = rd(1)
            if c:
                return c if isinstance(c, str) else chr(c[0])
    finally:
        if kbd_intr:
            kbd_intr(3)


def _pause_more():
    # MMBasic's ListNewLine pause: prompt, wait for a key, wipe the prompt and
    # clear the screen so the next page starts clean. Returns False to stop
    # (q or Ctrl-C), True to carry on.
    sys.stdout.write("PRESS ANY KEY ...")
    c = _getkey()
    sys.stdout.write("\r                 \r")  # erase the 17-char prompt
    if c in ("q", "Q", "\x03"):
        sys.stdout.write("\n")
        return False
    sys.stdout.write("\x1b[H\x1b[2J")  # home cursor + clear for the next page
    return True


def cat(path, page=True):
    """Print a text file's contents to the console. Pages a screenful at a time
    (press any key to continue, q or Ctrl-C to stop) so long files don't scroll
    off the HDMI display; pass page=False to dump the whole file continuously."""
    with open(path) as f:
        if not page:
            while True:
                chunk = f.read(256)
                if not chunk:
                    break
                print(chunk, end="")
            print()
            return
        cols, rows = _console_size()
        limit = rows - 1  # MMBasic keeps one line of overlap between pages
        count = 1         # the command line already sits at the top of page one
        for line in f:
            line = line.rstrip("\n")
            print(line)
            # A line wider than the screen wraps onto extra rows; count them so
            # the page break lands where the text actually fills the screen.
            count += len(line) // cols + 1
            if count >= limit:
                if not _pause_more():
                    return
                count = 0


def _isdir(path):
    try:
        return bool(os.stat(path)[0] & 0x4000)
    except OSError:
        return False


def _expand(path):
    # Shell-style glob expansion for the file commands: a wildcard in the last
    # path component expands to the sorted list of matching files (sub-dirs are
    # skipped, as cp/mv/rm act on files). No wildcard -> [path] unchanged, so a
    # missing plain path still surfaces as the operation's own OSError. A
    # wildcard that matches nothing raises, like a shell with a bad glob.
    directory, pattern = _split_pattern(path)
    if pattern is None:
        return [path]
    sep = "" if directory.endswith("/") else "/"
    matches = []
    for entry in os.ilistdir(directory):
        if entry[1] & 0x4000:  # directory
            continue
        if _glob_match(entry[0], pattern):
            matches.append(directory + sep + entry[0])
    if not matches:
        raise OSError("no matches: " + path)
    matches.sort()
    return matches


def _resolve_dest(src, dst):
    # If dst is an existing directory, target <dst>/<basename of src>.
    if _isdir(dst):
        base = src.rsplit("/", 1)[-1]
        return dst + ("" if dst.endswith("/") else "/") + base
    return dst


def _copy_file(src, dst):
    with open(src, "rb") as fi, open(dst, "wb") as fo:
        while True:
            block = fi.read(1024)
            if not block:
                break
            fo.write(block)


def cp(src, dst):
    """Copy a file. If dst is a directory, copy into it. A wildcard in src
    copies every matching file, e.g. cp("*.py", "/sd"); dst must then be a
    directory."""
    sources = _expand(src)
    if len(sources) > 1 and not _isdir(dst):
        raise OSError("target is not a directory: " + dst)
    for s in sources:
        _copy_file(s, _resolve_dest(s, dst))


def mv(src, dst):
    """Move/rename a file. If dst is a directory, move into it. A wildcard in
    src moves every matching file (dst must then be a directory). Falls back to
    copy+remove when src and dst are on different filesystems."""
    sources = _expand(src)
    if len(sources) > 1 and not _isdir(dst):
        raise OSError("target is not a directory: " + dst)
    for s in sources:
        d = _resolve_dest(s, dst)
        try:
            os.rename(s, d)
        except OSError:
            _copy_file(s, d)
            os.remove(s)


def autosave(path):
    """Capture what you type or paste at the console straight into a file, with
    no transfer protocol (MMBasic AUTOSAVE). Everything you send is written to
    `path`; end with Ctrl-Z (or Ctrl-D) to save, or Ctrl-C to cancel. Ideal for
    dropping a small program onto the board by pasting it into the terminal:

        autosave("hello.py")      # paste your code, press Ctrl-Z
        run("hello.py")
    """
    try:
        from micropython import kbd_intr
    except ImportError:
        kbd_intr = None
    rd = sys.stdin.buffer.read if hasattr(sys.stdin, "buffer") else sys.stdin.read
    wr = sys.stdout.write
    print("autosave to '%s' -- paste or type your program." % path)
    print("End with Ctrl-Z (or Ctrl-D) to save, Ctrl-C to cancel.")
    data = bytearray()
    aborted = False
    prev_cr = False
    if kbd_intr:
        kbd_intr(-1)  # deliver Ctrl-C/Ctrl-Z as raw bytes, not an interrupt
    try:
        while True:
            c = rd(1)
            if not c:
                continue
            b = c[0] if isinstance(c, (bytes, bytearray)) else ord(c)
            if b == 0x1A or b == 0x04:          # Ctrl-Z / Ctrl-D -> save
                break
            if b == 0x03:                       # Ctrl-C -> abort
                aborted = True
                break
            data.append(b)
            # Echo so a paste is visible, emitting exactly one CR/LF per line
            # break regardless of whether the source used CR, LF or CRLF.
            if b == 0x0D:
                wr("\r\n")
                prev_cr = True
            elif b == 0x0A:
                if not prev_cr:
                    wr("\r\n")
                prev_cr = False
            else:
                wr(chr(b))
                prev_cr = False
    finally:
        if kbd_intr:
            kbd_intr(3)  # restore the normal Ctrl-C interrupt
    if aborted:
        print("\nautosave cancelled -- nothing written.")
        return
    text = bytes(data).decode()
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    with open(path, "w") as f:
        f.write(text)
    lines = text.count("\n") + (1 if text and not text.endswith("\n") else 0)
    print("\nsaved %d bytes, %d line%s to %s" % (
        len(text), lines, "" if lines == 1 else "s", path))


# Commands injected into the REPL (__main__) namespace by _boot.py.
COMMANDS = ("ls", "run", "edit", "pwd", "cd", "mkdir", "rmdir", "rm", "cat", "cp",
            "mv", "autosave", "cls")
