# Dual-panel file manager for the Pico Computer 3 -- MMBasic's FM, reimagined
# with two panes so you can copy/move between directories. Browse the flash and
# SD filesystems and open files by type: run .py, play audio, show images,
# view/edit text. Works on the on-screen console (USB keyboard) and over serial.
#
#     fm()            # both panes start in the current directory
#     fm("/sd")       # both panes start on the SD card
#
# Keys: Tab toggles the active pane, Left/Right select the left/right pane;
# Up/Down/PgUp/PgDn/Home/End move; Enter opens (dir = enter, file =
# run/play/show/view by type); Backspace = up a directory. Space selects/deselects
# the highlighted file (MMBasic FM) so C/M/D act on the whole selection. C copy,
# M move (to the OTHER pane), E edit, V view, D delete, R rename, N new dir,
# P play / S stop audio, +/- volume, Q quits. Cursor moves repaint only the two
# changed rows.

import os
import sys

_AUDIO = (".wav", ".mp3", ".flac", ".mod")
_IMAGE = (".bmp", ".jpg", ".jpeg", ".png")
_TEXT = (".py", ".txt", ".csv", ".json", ".md", ".cfg", ".ini", ".log", ".bas",
         ".html", ".xml", ".sh")
_ANSI = {"A": "UP", "B": "DOWN", "C": "RIGHT", "D": "LEFT", "H": "HOME",
         "F": "END", "5~": "PGUP", "6~": "PGDN", "3~": "DEL"}
_LEGEND = ("Tab/<> pane", "Enter open", "Bksp up", "Space select", "C copy",
           "M move", "E edit", "D del", "R rename", "N mkdir", "S stop",
           "+- vol", "Q quit")


def _rd():
    return sys.stdin.read(1)


def _key():
    """One logical key, decoding ANSI escapes with blocking reads (an arrow's
    bytes arrive together). select.poll can't see the USB-keyboard ring buffer
    here, so a lone Esc can't be told from a sequence -- Q is the exit key."""
    c = _rd()
    if c == "\x03":
        return "QUIT"
    if c != "\x1b":
        if c in ("\r", "\n"):
            return "ENTER"
        if c in ("\x08", "\x7f"):
            return "BACK"
        if c == "\t":
            return "TAB"
        return c
    c2 = _rd()
    if c2 not in "[O":
        return "ESC"
    seq = ""
    for _ in range(6):
        c3 = _rd()
        seq += c3
        if "A" <= c3 <= "Z" or "a" <= c3 <= "z" or c3 == "~":
            break
    return _ANSI.get(seq, "ESC")


def _w(s):
    sys.stdout.write(s)


def _at(r, c):
    return "\x1b[%d;%dH" % (r, c)


def _ljust(s, n):                            # MicroPython str has no ljust/rjust
    return s + " " * (n - len(s)) if len(s) < n else s[:n]


def _hsize(n):
    if n < 1024:
        return str(n)
    if n < 1024 * 1024:
        return "%.0fK" % (n / 1024)
    return "%.1fM" % (n / 1048576)


def _abspath(p):
    if not p.startswith("/"):
        p = os.getcwd().rstrip("/") + "/" + p
    parts = []
    for seg in p.split("/"):
        if seg in ("", "."):
            continue
        if seg == "..":
            if parts:
                parts.pop()
        else:
            parts.append(seg)
    return "/" + "/".join(parts)


def _ext(name):
    d = name.rfind(".")
    return name[d:].lower() if d >= 0 else ""


class _Panel:
    def __init__(self, path, x, w, listrows):
        self.x = x
        self.w = w
        self.listrows = listrows
        self.sel = 0
        self.top = 0
        self.marked = set()          # Space-selected file names (this dir only)
        self.set_path(path)

    def set_path(self, path):
        self.path = _abspath(path)
        self.sel = 0
        self.top = 0
        self.marked.clear()
        self.load()

    def load(self):
        dirs = []
        files = []
        try:
            for e in os.ilistdir(self.path):
                isdir = bool(e[1] & 0x4000)
                size = e[3] if len(e) > 3 else 0
                (dirs if isdir else files).append((e[0], isdir, size))
        except OSError:
            pass
        dirs.sort(key=lambda x: x[0].lower())
        files.sort(key=lambda x: x[0].lower())
        parent = [] if self.path == "/" else [("..", True, 0)]
        self.entries = parent + dirs + files
        # drop marks on files that no longer exist (moved/deleted/renamed)
        self.marked &= {e[0] for e in self.entries if not e[1]}
        if self.sel >= len(self.entries):
            self.sel = max(0, len(self.entries) - 1)

    def full(self, name):
        b = self.path.rstrip("/")
        return (b + "/" + name) if b else "/" + name

    def cur(self):
        return self.entries[self.sel] if self.entries else ("", False, 0)

    def move(self, d):
        old = self.sel
        n = len(self.entries)
        if n:
            self.sel = max(0, min(n - 1, self.sel + d))
        scrolled = False
        if self.sel < self.top:
            self.top = self.sel
            scrolled = True
        elif self.sel >= self.top + self.listrows:
            self.top = self.sel - self.listrows + 1
            scrolled = True
        return old, scrolled

    def _row_str(self, idx):
        name, isdir, size = self.entries[idx]
        label = (name + "/") if isdir else name
        info = "<dir>" if isdir else _hsize(size)
        nw = self.w - len(info) - 1
        return _ljust(label[:nw], nw) + " " + info

    def draw_header(self, active):
        head = (" " + self.path)[:self.w]
        _w(_at(1, self.x) + ("\x1b[7m" if active else "\x1b[1m")
           + _ljust(head, self.w) + "\x1b[0m")

    def draw_row(self, i, active):
        idx = self.top + i
        _w(_at(2 + i, self.x))
        if idx >= len(self.entries):
            _w(" " * self.w)
            return
        s = self._row_str(idx)
        marked = self.entries[idx][0] in self.marked
        if idx == self.sel:
            attr = "\x1b[7m" if active else "\x1b[44m"
            _w(attr + ("\x1b[33m" if marked else "") + s + "\x1b[0m")
        elif marked:
            _w("\x1b[33m" + s + "\x1b[0m")
        else:
            _w(s)

    def draw(self, active):
        self.draw_header(active)
        for i in range(self.listrows):
            self.draw_row(i, active)

    def draw_sel(self, active):
        self.draw_row(self.sel - self.top, active)


class _FM:
    def __init__(self, path):
        import pcshell

        self.cols, self.rows = pcshell._console_size()
        self.legend = self._pack_legend()
        self.listrows = self.rows - 2 - len(self.legend)
        self.pw = (self.cols - 1) // 2
        base = _abspath(path or os.getcwd())
        self.left = _Panel(base, 1, self.pw, self.listrows)
        self.right = _Panel(base, self.pw + 2, self.pw, self.listrows)
        self.act = self.left
        self.status = ""

    def other(self):
        return self.right if self.act is self.left else self.left

    def _pack_legend(self):
        """Greedily pack the key legend into lines that fit the screen width, so
        every command stays visible even on a narrow (40-column) display."""
        lines = []
        cur = ""
        for it in _LEGEND:
            cand = it if not cur else cur + "  " + it
            if len(cand) <= self.cols:
                cur = cand
            else:
                lines.append(cur)
                cur = it
        if cur:
            lines.append(cur)
        return lines

    def _show_name(self):
        """Put the selected entry's full name in the status line (so a name too
        long for the pane is still readable) -- MMBasic FM does this."""
        name, isdir, size = self.act.cur()
        s = ""
        if name:
            s = name + ("  <dir>" if isdir else "  " + _hsize(size))
        n = len(self.act.marked)
        if n:
            s += "  [%d selected]" % n
        self.set_status(s)

    # --- rendering ---
    def redraw(self):
        _w("\x1b[?25l\x1b[2J")
        self.left.draw(self.act is self.left)
        self.right.draw(self.act is self.right)
        _w(_at(1, self.pw + 1) + "\x1b[1m|\x1b[0m")
        self.footer()
        self._show_name()

    def footer(self):
        nl = len(self.legend)                        # legend fills the rows just
        for i in range(nl):                          # above the status line
            _w(_at(self.rows - nl + i, 1) + "\x1b[2K" + self.legend[i])
        _w(_at(self.rows, 1) + "\x1b[2K" + self.status[:self.cols])

    def set_status(self, s):
        self.status = s
        _w(_at(self.rows, 1) + "\x1b[2K" + s[:self.cols])

    def _cursor(self, d):
        a = self.act
        old, scrolled = a.move(d)
        if scrolled:
            a.draw(True)
        elif old != a.sel:
            a.draw_row(old - a.top, True)
            a.draw_row(a.sel - a.top, True)
        self._show_name()

    def _activate(self, panel):
        if self.act is panel:
            return
        self.act = panel
        self.left.draw_header(self.act is self.left)
        self.right.draw_header(self.act is self.right)
        self.left.draw_sel(self.act is self.left)
        self.right.draw_sel(self.act is self.right)
        self._show_name()

    # --- prompts ---
    def _prompt(self, msg, initial=""):
        buf = list(initial)
        _w(_at(self.rows, 1) + "\x1b[2K\x1b[?25h" + msg + initial)
        while True:
            k = _key()
            if k == "ENTER":
                _w("\x1b[?25l")
                return "".join(buf)
            if k in ("ESC", "QUIT"):
                _w("\x1b[?25l")
                return None
            if k == "BACK":
                if buf:
                    buf.pop()
                    _w("\b \b")
            elif isinstance(k, str) and len(k) == 1 and 32 <= ord(k) < 127:
                buf.append(k)
                _w(k)

    def _confirm(self, msg):
        _w(_at(self.rows, 1) + "\x1b[2K" + msg)
        return _key() in ("y", "Y")

    def _pause(self, msg="press a key"):
        _w("\r\n" + msg + " ...")
        _key()

    # --- opening / actions ---
    def open_sel(self):
        a = self.act
        name, isdir, _ = a.cur()
        if not name:
            return
        if isdir:
            a.set_path(a.path + "/.." if name == ".." else a.full(name))
            a.draw(True)
            self._show_name()
            return
        ext = _ext(name)
        full = a.full(name)
        if ext == ".py":
            self.run(full)
            self.redraw()
        elif ext in _AUDIO:
            self.play(full, name)
        elif ext in _IMAGE:
            self.show(full, ext)
            self.redraw()
        elif ext in _TEXT:
            self.view(full)
            self.redraw()
        else:
            self.set_status("no action for " + name)

    def up(self):
        self.act.set_path(self.act.path + "/..")
        self.act.draw(True)
        self._show_name()

    def toggle_mark(self):
        """Space -- select/deselect the highlighted file (MMBasic FM), then step
        down a row so repeated presses sweep a range. C/M/D act on the whole
        selection; entering another directory clears it."""
        a = self.act
        name, isdir, _ = a.cur()
        if not name or isdir:
            self.set_status("only files can be selected")
            return
        if name in a.marked:
            a.marked.discard(name)
        else:
            a.marked.add(name)
        a.draw_sel(True)         # recolour in place (cursor may be on last row)
        self._cursor(1)

    def run(self, full):
        import pcshell
        import hdmi

        before = (hdmi.width(), hdmi.height(), hdmi.bpp())
        _w("\x1b[2J\x1b[H\x1b[?25h")
        try:
            pcshell.run(full)
        except Exception as e:
            sys.print_exception(e)
        if (hdmi.width(), hdmi.height(), hdmi.bpp()) != before:
            self._restore_display()
        else:
            self._pause("finished -- press a key")
            import pcconsole

            pcconsole.console()

    def _restore_display(self):
        import hdmi
        import pcconfig
        import pcconsole
        import pcshell

        hdmi.deinit()
        try:
            hdmi.init(pcconfig.get("hdmi_mode", hdmi.RGB640),
                      pcconfig.get("hdmi_clock", 252))
        except Exception:
            hdmi.init(hdmi.RGB640)
        pcconsole.console()
        self.cols, self.rows = pcshell._console_size()
        # the mode may have changed the console geometry -> re-lay-out
        self.legend = self._pack_legend()
        self.listrows = self.rows - 2 - len(self.legend)
        self.pw = (self.cols - 1) // 2
        for p, x in ((self.left, 1), (self.right, self.pw + 2)):
            p.x = x
            p.w = self.pw
            p.listrows = self.listrows

    def play(self, full, name):
        import pcaudio

        try:
            pcaudio.play(full)
            self.set_status("playing " + name + "   (S stop)")
        except Exception as e:
            self.set_status("audio error: " + str(e))

    def show(self, full, ext):
        import pcimage

        _w("\x1b[2J")
        try:
            if ext in (".jpg", ".jpeg"):
                pcimage.draw_jpg(full)
            elif ext == ".bmp":
                pcimage.draw_bmp(full)
            elif ext == ".png":
                pcimage.draw_png(full)
        except Exception as e:
            self.set_status("image error: " + str(e))
        _key()

    def view(self, full):
        import pcshell

        _w("\x1b[2J\x1b[H\x1b[?25h")
        try:
            pcshell.cat(full)
        except Exception as e:
            sys.print_exception(e)
        self._pause()

    def edit(self):
        import pcshell

        name, isdir, _ = self.act.cur()
        if isdir:
            return
        _w("\x1b[?25h")
        try:
            pcshell.edit(self.act.full(name))
        except Exception as e:
            sys.print_exception(e)
        self.act.load()
        self.redraw()

    def copy(self, move=False):
        import pcshell

        a = self.act
        dst = self.other()
        verb = "move" if move else "copy"
        if a.marked:
            names = sorted(a.marked, key=lambda n: n.lower())
        else:
            name, isdir, _ = a.cur()
            if not name or isdir:
                self.set_status("select a file to " + verb)
                return
            names = [name]
        if dst.path == a.path:
            self.set_status("both panes show the same directory")
            return
        done = 0
        err = None
        for name in names:
            dstfull = dst.full(name)
            try:
                if move:
                    try:
                        os.rename(a.full(name), dstfull)
                    except OSError:
                        pcshell._copy_file(a.full(name), dstfull)
                        os.remove(a.full(name))
                else:
                    pcshell._copy_file(a.full(name), dstfull)
                done += 1
            except OSError as e:
                err = e
        a.marked.clear()
        a.load()
        a.draw(True)
        dst.load()
        dst.draw(False)
        what = names[0] if len(names) == 1 else "%d files" % done
        s = ("moved " if move else "copied ") + what + " -> " + dst.path
        if err is not None:
            s = "%s failed: %s" % (verb, err) if done == 0 else s + "  (1+ failed: %s)" % err
        self.set_status(s)

    def delete(self):
        a = self.act
        if a.marked:
            names = sorted(a.marked, key=lambda n: n.lower())
            if not self._confirm("Delete %d selected files? (y/N) " % len(names)):
                self._show_name()
                return
            done = 0
            err = None
            for name in names:
                try:
                    os.remove(a.full(name))
                    done += 1
                except OSError as e:
                    err = e
            a.marked.clear()
            s = "deleted %d file%s" % (done, "" if done == 1 else "s")
            if err is not None:
                s += "  (1+ failed: %s)" % err
            self.set_status(s)
            a.load()
            a.draw(True)
            return
        name, isdir, _ = a.cur()
        if not name or name == "..":
            return
        if self._confirm("Delete '%s'? (y/N) " % name):
            try:
                (os.rmdir if isdir else os.remove)(a.full(name))
                self.set_status("deleted " + name)
            except OSError as e:
                self.set_status("delete failed: " + str(e))
            a.load()
            a.draw(True)

    def rename(self):
        a = self.act
        name, isdir, _ = a.cur()
        if not name or name == "..":
            return
        new = self._prompt("Rename '%s' to: " % name)
        if new:
            try:
                os.rename(a.full(name), a.full(new))
            except OSError as e:
                self.set_status("rename failed: " + str(e))
            a.load()
            a.draw(True)

    def mkdir(self):
        a = self.act
        new = self._prompt("New directory: ")
        if new:
            try:
                os.mkdir(a.full(new))
            except OSError as e:
                self.set_status("mkdir failed: " + str(e))
            a.load()
            a.draw(True)

    def _vol(self, d):
        import pcaudio

        v = pcaudio.volume()
        v = (v if v is not None else 50) + d * 10
        pcaudio.volume(max(0, min(100, v)))
        self.set_status("volume %d" % pcaudio.volume())

    # --- main loop ---
    def loop(self):
        self.redraw()
        while True:
            k = _key()
            a = self.act
            lr = a.listrows
            if k in ("Q", "q", "QUIT"):
                return
            elif k == "TAB":
                self._activate(self.other())
            elif k == "LEFT":
                self._activate(self.left)
            elif k == "RIGHT":
                self._activate(self.right)
            elif k == "UP":
                self._cursor(-1)
            elif k == "DOWN":
                self._cursor(1)
            elif k == "PGUP":
                self._cursor(-lr)
            elif k == "PGDN":
                self._cursor(lr)
            elif k == "HOME":
                self._cursor(-a.sel)
            elif k == "END":
                self._cursor(len(a.entries) - 1 - a.sel)
            elif k == "BACK":
                self.up()
            elif k == " ":
                self.toggle_mark()
            elif k == "ENTER":
                self.open_sel()
            elif k in ("C", "c"):
                self.copy()
            elif k in ("M", "m"):
                self.copy(move=True)
            elif k in ("E", "e"):
                self.edit()
            elif k in ("D", "d", "DEL"):
                self.delete()
            elif k in ("R", "r"):
                self.rename()
            elif k in ("N", "n"):
                self.mkdir()
            elif k in ("S", "s"):
                import pcaudio

                pcaudio.stop()
                self.set_status("stopped")
            elif k in ("+", "="):
                self._vol(1)
            elif k in ("-", "_"):
                self._vol(-1)


def fm(path=None):
    """Open the dual-panel file manager (MMBasic FM). `path` is the starting
    directory (default: the current directory). Tab switches panes; Space
    selects several files for one C/M/D copy/move/delete; Q exits."""
    try:
        from micropython import kbd_intr
    except ImportError:
        kbd_intr = None
    if kbd_intr:
        kbd_intr(-1)
    try:
        _FM(path).loop()
    finally:
        if kbd_intr:
            kbd_intr(3)
        _w("\x1b[2J\x1b[H\x1b[?25h\x1b[0m")
