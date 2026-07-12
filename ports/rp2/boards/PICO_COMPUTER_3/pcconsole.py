# On-screen text console for the Pico Computer 3. Renders console output into the
# HDMI framebuffer using framebuf's built-in 8x8 font, and attaches via
# os.dupterm() so the REPL's output is mirrored to the monitor. Keyboard input
# still comes from the UART (a USB-host keyboard is a later step).
#
#   console()        # start: REPL output now also appears on the HDMI screen
#   console(False)   # stop
#
# ANSI/VT100 escape sequences (CSI) are swallowed rather than rendered, so the
# screen stays clean; this is a plain scrolling text console, not a full terminal
# emulator (pye's full-screen editing won't render on-screen yet).

import io
import os
import sys
import hdmi
import machine

_FW = 8    # MMBasic 8x12 console font cell width
_FH = 12   # ... and height -> 80x40 chars at 640x480


def sync_terminal():
    """Resize the attached serial terminal to match the screen's character grid
    (xterm CSI 8;rows;cols t), so the terminal and HDMI console stay in sync."""
    cols = hdmi.width() // _FW
    rows = hdmi.height() // _FH
    sys.stdout.write("\x1b[8;%d;%dt" % (rows, cols))


class Console(io.IOBase):
    def __init__(self, fg=0xFFFFFF, bg=0x000000):
        self._fg888 = fg  # keep RGB888 so colours can be re-derived after a mode change
        self._bg888 = bg
        self._gen = hdmi.gen()
        self.d = hdmi.fb()
        self.w = hdmi.width()
        self.h = hdmi.height()
        self.cols = self.w // _FW
        self.rows = self.h // _FH
        self.fg = self.d.colour(fg)
        self.bg = self.d.colour(bg)
        self.x = 0  # cursor column
        self.y = 0  # cursor row
        self.esc = 0  # escape-sequence state: 0 none, 1 saw ESC, 2 in CSI
        self._cur_on = False       # underline cursor currently drawn?
        self._cur_x = 0
        self._cur_y = 0
        self._in_write = False     # suppress blink while rendering
        self.d.fill(self.bg)
        self._draw_cursor()
        # Blink the cursor ~1 Hz. Callback is scheduled (runs between bytecodes),
        # so it can't interleave mid-render; _in_write guards it anyway.
        self._timer = machine.Timer(-1, mode=machine.Timer.PERIODIC, period=500,
                                    callback=self._blink)

    def _newline(self):
        self.x = 0
        self.y += 1
        if self.y >= self.rows:
            # Fast C bulk-memmove scroll (framebuf.scroll is per-pixel and far
            # too slow); it also clears the exposed bottom line to bg.
            hdmi.scroll(_FH, self.bg)
            self.y = self.rows - 1

    def _putc(self, c):
        if self.esc == 2:  # inside a CSI sequence, collecting parameters
            if 0x30 <= c <= 0x39:  # digit
                self.pcur = self.pcur * 10 + (c - 0x30)
                self.phas = True
            elif c == 0x3B:  # ';' parameter separator
                self.params.append(self.pcur)
                self.pcur = 0
                self.phas = False
            elif 0x40 <= c <= 0x7E:  # final byte -> dispatch
                if self.phas:
                    self.params.append(self.pcur)
                self._csi(c, self.params)
                self.esc = 0
            # else intermediate/private byte (e.g. '?'): ignore, keep collecting
            return
        if self.esc == 1:  # just saw ESC
            if c == 0x5B:  # '[' -> CSI
                self.esc = 2
                self.params = []
                self.pcur = 0
                self.phas = False
            else:
                self.esc = 0  # other ESC-x sequences: ignore
            return
        if c == 0x1B:  # ESC
            self.esc = 1
        elif c == 0x0A:  # \n
            self._newline()
        elif c == 0x0D:  # \r
            self.x = 0
        elif c == 0x08:  # backspace (cursor left, no erase)
            if self.x > 0:
                self.x -= 1
        elif c == 0x09:  # tab -> next multiple of 4
            self.x = (self.x + 4) & ~3
            if self.x >= self.cols:
                self._newline()
        elif 0x20 <= c <= 0x7E:  # printable -> 8x12 glyph (opaque, clears the cell)
            hdmi.putc(self.x * _FW, self.y * _FH, c, self.fg, self.bg)
            self.x += 1
            if self.x >= self.cols:
                self._newline()

    def _csi(self, final, p):
        n = p[0] if p and p[0] else 1
        if final == 0x44:  # 'D' cursor back
            self.x = max(0, self.x - n)
        elif final == 0x43:  # 'C' cursor forward
            self.x = min(self.cols - 1, self.x + n)
        elif final == 0x41:  # 'A' cursor up
            self.y = max(0, self.y - n)
        elif final == 0x42:  # 'B' cursor down
            self.y = min(self.rows - 1, self.y + n)
        elif final == 0x48 or final == 0x66:  # 'H'/'f' cursor position (1-based)
            row = (p[0] if len(p) >= 1 and p[0] else 1) - 1
            col = (p[1] if len(p) >= 2 and p[1] else 1) - 1
            self.y = min(max(0, row), self.rows - 1)
            self.x = min(max(0, col), self.cols - 1)
        elif final == 0x4B:  # 'K' erase in line
            self._erase_line(p[0] if p else 0)
        elif final == 0x4A:  # 'J' erase in display
            self._erase_display(p[0] if p else 0)
        # 'm' (colours), 'n' (device queries), private '?...' etc: ignored

    def _erase_line(self, mode):
        py = self.y * _FH
        if mode == 1:  # start of line .. cursor
            self.d.fill_rect(0, py, (self.x + 1) * _FW, _FH, self.bg)
        elif mode == 2:  # whole line
            self.d.fill_rect(0, py, self.w, _FH, self.bg)
        else:  # 0: cursor .. end of line
            px = self.x * _FW
            self.d.fill_rect(px, py, self.w - px, _FH, self.bg)

    def _erase_display(self, mode):
        if mode == 0:  # cursor .. end of screen
            self._erase_line(0)
            y2 = (self.y + 1) * _FH
            if y2 < self.h:
                self.d.fill_rect(0, y2, self.w, self.h - y2, self.bg)
        elif mode == 1:  # start of screen .. cursor
            self._erase_line(1)
            if self.y > 0:
                self.d.fill_rect(0, 0, self.w, self.y * _FH, self.bg)
        else:  # 2/3: whole screen
            self.d.fill(self.bg)

    def _draw_cursor(self):
        if not self._cur_on:
            self.d.fill_rect(self.x * _FW, self.y * _FH + _FH - 1, _FW, 1, self.fg)
            self._cur_x = self.x
            self._cur_y = self.y
            self._cur_on = True

    def _erase_cursor(self):
        if self._cur_on:
            self.d.fill_rect(self._cur_x * _FW, self._cur_y * _FH + _FH - 1, _FW, 1, self.bg)
            self._cur_on = False

    def _blink(self, t):
        if self._in_write:
            return
        if self._cur_on:
            self._erase_cursor()
        else:
            self._draw_cursor()

    def deinit(self):
        try:
            self._timer.deinit()
        except Exception:
            pass

    def _resync(self):
        # A mode/clock switch (hdmi.init) cleared the framebuffer and may have
        # changed the geometry. Re-read everything and home the cursor so the
        # next output starts clean at 0,0.
        self._gen = hdmi.gen()
        self.d = hdmi.fb()
        self.w = hdmi.width()
        self.h = hdmi.height()
        self.cols = self.w // _FW
        self.rows = self.h // _FH
        self.fg = self.d.colour(self._fg888)
        self.bg = self.d.colour(self._bg888)
        self.x = 0
        self.y = 0
        self.esc = 0
        self._cur_on = False  # old cursor was wiped by the clear

    # Stream protocol for os.dupterm().
    def write(self, buf):
        if self._gen != hdmi.gen():
            self._resync()
        self._in_write = True
        self._erase_cursor()          # never leave the cursor behind during output
        for c in buf:
            self._putc(c)
        self._draw_cursor()
        self._in_write = False
        return len(buf)

    def readinto(self, buf):
        return None  # no keyboard on the screen (input is on the UART)


_con = None


def console(target=True, fg=0xFFFFFF, bg=0x000000):
    """Route console output (MMBasic OPTION CONSOLE). target is one of:
      "both"   - HDMI screen + serial port (the power-up default)
      "screen" - HDMI screen only (serial output muted; input still works)
      "serial" - serial port only (nothing printed on the HDMI screen --
                 handy while testing graphics)
    True/False are accepted as shorthand for "both"/"serial". Keyboard input
    (USB and serial) is never affected. Returns the on-screen Console (or
    None for "serial")."""
    global _con
    if target is True:
        target = "both"
    elif target is False:
        target = "serial"
    if target not in ("both", "screen", "serial"):
        raise ValueError("console target must be 'both', 'screen' or 'serial'")
    try:
        import _sercon

        _sercon.mute(target == "screen")
    except ImportError:
        pass  # no serial console in this build: screen behaves like both
    if _con is not None:  # tear down any existing console (stops its blink timer)
        _con.deinit()
        _con = None
    if target != "serial":
        _con = Console(fg, bg)
        sync_terminal()
        os.dupterm(_con)
        return _con
    os.dupterm(None)
