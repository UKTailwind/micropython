# Shell-style helpers for the Pico Computer 3. Injected into the REPL namespace
# by _boot.py so they can be used without an explicit import.

import os
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


def run(path):
    """Launch a Python program from a file.

    Runs with __name__ == "__main__" (so `if __name__ == "__main__":` blocks
    fire) and with the working directory temporarily set to the program's
    folder, restored afterwards. The program's namespace is seeded with a COPY
    of the REPL globals, so the injected helpers (touch, ls, play, hdmi, the
    colour palette, ...) are available exactly as at the prompt — but since it's
    a copy, the program can't clobber the real REPL globals."""
    import __main__

    slash = path.rfind("/")
    folder = path[:slash] if slash > 0 else ("/" if slash == 0 else None)
    with open(path) as f:
        source = f.read()
    g = {k: v for k, v in __main__.__dict__.items() if not k.startswith("__")}
    g["__name__"] = "__main__"
    g["__file__"] = path
    cwd = os.getcwd()
    try:
        if folder is not None:
            os.chdir(folder)
        exec(source, g)
    finally:
        os.chdir(cwd)


def edit(*args, **kwargs):
    """Launch the pye full-screen editor. edit("/sd/foo.py") opens (or creates)
    a file; Ctrl-S saves, Ctrl-Q quits. pye is imported lazily so it costs no
    RAM until first use."""
    import pye

    return pye.pye(*args, **kwargs)


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
    """Remove a file."""
    os.remove(path)


def cat(path):
    """Print a text file's contents to the console."""
    with open(path) as f:
        while True:
            chunk = f.read(256)
            if not chunk:
                break
            print(chunk, end="")
    print()


def _resolve_dest(src, dst):
    # If dst is an existing directory, target <dst>/<basename of src>.
    try:
        if os.stat(dst)[0] & 0x4000:
            base = src.rsplit("/", 1)[-1]
            dst = dst + ("" if dst.endswith("/") else "/") + base
    except OSError:
        pass
    return dst


def cp(src, dst):
    """Copy a file. If dst is a directory, copy into it."""
    dst = _resolve_dest(src, dst)
    with open(src, "rb") as fi, open(dst, "wb") as fo:
        while True:
            block = fi.read(1024)
            if not block:
                break
            fo.write(block)


def mv(src, dst):
    """Move/rename a file. If dst is a directory, move into it. Falls back to
    copy+remove when src and dst are on different filesystems."""
    dst = _resolve_dest(src, dst)
    try:
        os.rename(src, dst)
    except OSError:
        cp(src, dst)
        os.remove(src)


# Commands injected into the REPL (__main__) namespace by _boot.py.
COMMANDS = ("ls", "run", "edit", "pwd", "cd", "mkdir", "rmdir", "rm", "cat", "cp", "mv")
