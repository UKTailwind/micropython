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

# 16-colour ANSI palette (RGB888): 0-7 normal, 8-15 bright. SGR codes
# 30-37/40-47 pick 0-7, 90-97/100-107 (or bold) pick 8-15. The bright half is
# the pure primaries, so it matches MMBasic's editor colours and maps exactly
# to the RGB1024 16-colour palette. Converted to the framebuffer's native
# format per mode via Display.colour().
_ANSI = (
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
)


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
        self._reset_sgr()  # current SGR fg/bg (start = default fg/bg)
        self.x = 0  # cursor column
        self.y = 0  # cursor row
        self.rtop = 0            # scroll-region top row (0-based, inclusive)
        self.rbot = self.rows - 1  # scroll-region bottom row (0-based, inclusive)
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

    def _scroll_region(self, direction):
        # direction +1: region content moves up one text row (blank at the
        # bottom of the region); -1: content moves down (blank at the top).
        # Only the scroll region's pixel band moves, so pye's status line
        # (below the region) is left untouched. Fast C band memmove.
        y0 = self.rtop * _FH
        band = (self.rbot - self.rtop + 1) * _FH
        hdmi.scroll(direction * _FH, self.bg, y0, band)

    def _newline(self):
        # CR+LF. At the bottom of the scroll region, scroll the region up;
        # otherwise just move down (stopping at the last screen row).
        self.x = 0
        if self.y == self.rbot:
            self._scroll_region(1)
        elif self.y < self.rows - 1:
            self.y += 1

    def _reverse_index(self):
        # ESC M: at the top of the region, scroll the region down; else up one.
        if self.y == self.rtop:
            self._scroll_region(-1)
        elif self.y > 0:
            self.y -= 1

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
                if c == 0x4D:  # 'M' reverse index (RI) -> scroll region down
                    self._reverse_index()
                elif c == 0x44:  # 'D' index (IND) -> like LF but keep column
                    col = self.x
                    self._newline()
                    self.x = col
                # other ESC-x sequences: ignore
                self.esc = 0
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
            hdmi.putc(self.x * _FW, self.y * _FH, c, self.cur_fg, self.cur_bg)
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
        elif final == 0x6D:  # 'm' SGR: colour / attributes
            self._sgr(p)
        elif final == 0x72:  # 'r' DECSTBM set scroll region (1-based rows)
            if len(p) >= 2 and p[0] and p[1]:
                top = min(max(0, p[0] - 1), self.rows - 1)
                bot = min(max(0, p[1] - 1), self.rows - 1)
                if top < bot:
                    self.rtop = top
                    self.rbot = bot
            else:  # no params -> reset to the whole screen
                self.rtop = 0
                self.rbot = self.rows - 1
            # DECSTBM homes the cursor to the screen's top-left (VT100).
            self.x = 0
            self.y = 0
        # 'm' (colours), 'n' (device queries), private '?...' etc: ignored

    def _reset_sgr(self):
        # Colour state: fg/bg palette indices (-1 = the console default),
        # plus bold (bright) and reverse-video flags. cur_fg/cur_bg are the
        # resolved native-format colours used to render each glyph.
        self._bold = False
        self._rev = False
        self._fgi = -1
        self._bgi = -1
        self.cur_fg = self.fg
        self.cur_bg = self.bg

    def _sgr(self, params):
        # Interpret an SGR (Select Graphic Rendition) escape: fg 30-37/90-97,
        # bg 40-47/100-107, 0 reset, 1 bold, 7 reverse (and their off codes).
        for p in params or (0,):
            if p == 0:
                self._bold = self._rev = False
                self._fgi = self._bgi = -1
            elif p == 1:
                self._bold = True
            elif p == 22:
                self._bold = False
            elif p == 7:
                self._rev = True
            elif p == 27:
                self._rev = False
            elif 30 <= p <= 37:
                self._fgi = p - 30
            elif p == 39:
                self._fgi = -1
            elif 90 <= p <= 97:
                self._fgi = p - 90 + 8
            elif 40 <= p <= 47:
                self._bgi = p - 40
            elif p == 49:
                self._bgi = -1
            elif 100 <= p <= 107:
                self._bgi = p - 100 + 8
        # Resolve to native colours (bold brightens a normal 0-7 foreground).
        fgi = self._fgi
        if 0 <= fgi < 8 and self._bold:
            fgi += 8
        fg = self.fg if fgi < 0 else self.d.colour(_ANSI[fgi])
        bg = self.bg if self._bgi < 0 else self.d.colour(_ANSI[self._bgi])
        self.cur_fg, self.cur_bg = (bg, fg) if self._rev else (fg, bg)

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
        self._reset_sgr()  # native colours differ in the new format; recompute
        self.x = 0
        self.y = 0
        self.rtop = 0
        self.rbot = self.rows - 1  # region invalid at the new geometry -> reset
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
      "none"   - no console output at all: nothing on the screen (and no
                 blinking cursor over full-screen graphics), serial muted.
                 Input still works everywhere; restore with console().
    True/False are accepted as shorthand for "both"/"serial". Keyboard input
    (USB and serial) is never affected. Returns the on-screen Console (or
    None for "serial"/"none")."""
    global _con
    if target is True:
        target = "both"
    elif target is False:
        target = "serial"
    if target not in ("both", "screen", "serial", "none"):
        raise ValueError(
            "console target must be 'both', 'screen', 'serial' or 'none'")
    try:
        import _sercon

        _sercon.mute(target in ("screen", "none"))
    except ImportError:
        pass  # no serial console in this build: screen behaves like both
    if _con is not None:  # tear down any existing console (stops its blink timer)
        _con.deinit()
        _con = None
    if target in ("both", "screen"):
        _con = Console(fg, bg)
        sync_terminal()
        os.dupterm(_con)
        return _con
    os.dupterm(None)
