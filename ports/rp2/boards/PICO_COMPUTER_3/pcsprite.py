# Sprite engine for the Pico Computer 3.
#
# MMBasic's sprite SEMANTICS (layer-partitioned AABB collisions, layer 0
# collides with everything and scrolls with the background, edge and wall
# collisions, edge-triggered reporting, deferred moves committed atomically)
# on a modern rendering architecture: instead of MMBasic's save-under LIFO
# stacks (SHOW/HIDE SAFE, hide-all/redraw-all), sprites are composited each
# update() with dirty-rectangle erase + z-ordered redraw:
#
#   RGB320  - sprites live on the overlay LAYER; erasing is a fill with the
#             layer's transparent colour, so the scenery on the display is
#             never touched at all.
#   others  - the F buffer holds a snapshot of the scenery; erasing blits the
#             patch back from F ("dirty rectangle" engine).
#
# All pixel work is C (hdmi.blit / fill_rect); Python only orchestrates
# rectangles. Typical loop:
#
#   import pcsprite as sp
#   ship = sp.sheet("/sd/ships.png", 16, 16, count=4)[0]
#   ship.show(150, 100)
#   while True:
#       ship.x += dx; ship.y += dy       # nothing drawn yet
#       for a, b in sp.update(vsync=True):   # commit + collisions
#           ...
#
import hdmi
import framebuf

_shown = []        # visible sprites in z-order (last = topmost)
_walls = []        # static collision rectangles (MMBasic static objects)
_dirty = []        # rectangles to erase/redraw at the next update()
_all_dirty = False
_mode = None       # None until first update(): then "L" or "F"
_ldisp = None      # Display over the layer ("L" mode) for fill_rect erase
_erase = 0         # native colour that erases the layer (its transparent)
_gen = -1          # hdmi.gen() at setup; a mode change resets the engine
_cb = None         # optional collision callback
_strip = None      # cached scroll wrap-strip buffer


def _fbformat():
    bpp = hdmi.bpp()
    if bpp == 4:
        return framebuf.GS4_HMSB
    return framebuf.GS8 if bpp == 8 else framebuf.RGB565


def _imgbytes(w, h):
    # A mode-format pixel buffer for a w x h image (4bpp needs even width).
    bpp = hdmi.bpp()
    if bpp == 4:
        if w & 1:
            raise ValueError("sprite width must be even in RGB1024")
        return bytearray(w * h // 2)
    return bytearray(w * h * (bpp // 8))


class Sprite:
    """One sprite: an image (mode-format bytearray) plus position/layer.
    Set .x/.y freely; nothing is drawn until pcsprite.update()."""

    def __init__(self, img, w, h, transparent=None):
        self.img = img
        self.w = w
        self.h = h
        # Native-format colour treated as see-through when drawing (None = opaque).
        self.transparent = -1 if transparent is None else transparent
        self.x = 0
        self.y = 0
        self.layer = 1     # 0 = collides with all layers and scrolls with scenery
        self._sx = None    # position as last drawn (None = not on screen)
        self._sy = None
        self._touch = set()  # current contacts, for edge-triggered reporting

    def show(self, x=None, y=None, layer=None):
        """Make the sprite visible (topmost). Draws at the next update()."""
        if x is not None:
            self.x = x
        if y is not None:
            self.y = y
        if layer is not None:
            self.layer = layer
        if self not in _shown:
            _shown.append(self)
        return self

    def hide(self):
        """Remove from the screen at the next update()."""
        if self in _shown:
            _shown.remove(self)
            if self._sx is not None:
                _dirty.append((self._sx, self._sy, self.w, self.h))
            self._sx = None
            self._touch.clear()

    @property
    def visible(self):
        return self in _shown

    def top(self):
        """Raise to the top of the z-order."""
        if self in _shown:
            _shown.remove(self)
            _shown.append(self)
            if self._sx is not None:
                _dirty.append((self._sx, self._sy, self.w, self.h))
        return self

    def flip(self, direction="h"):
        """A new Sprite with the image mirrored: "h", "v" or "hv"."""
        src = framebuf.FrameBuffer(self.img, self.w, self.h, _fbformat())
        img = _imgbytes(self.w, self.h)
        dst = framebuf.FrameBuffer(img, self.w, self.h, _fbformat())
        fh = "h" in direction
        fv = "v" in direction
        for yy in range(self.h):
            for xx in range(self.w):
                dst.pixel(self.w - 1 - xx if fh else xx,
                          self.h - 1 - yy if fv else yy, src.pixel(xx, yy))
        t = self.transparent
        return Sprite(img, self.w, self.h, None if t < 0 else t)

    def touching(self, other):
        """AABB test against another Sprite right now (ignores layers)."""
        return not (other.x + other.w < self.x or other.x > self.x + self.w
                    or other.y + other.h < self.y or other.y > self.y + self.h)


class Wall:
    """A static collision rectangle (MMBasic static object). Not drawn."""

    def __init__(self, x, y, w, h):
        self.x = x
        self.y = y
        self.w = w
        self.h = h

    def remove(self):
        if self in _walls:
            _walls.remove(self)


def wall(x, y, w, h):
    """Register a static collision rectangle; returns the Wall."""
    wl = Wall(x, y, w, h)
    _walls.append(wl)
    return wl


def grab(x, y, w, h, transparent=None, src=None):
    """A new Sprite whose image is copied from a surface region (default: the
    current write target). E.g. draw art with fb(), then grab() it."""
    img = _imgbytes(w, h)
    hdmi.blit(x, y, w, h, 0, 0, src, (img, w, h))
    return Sprite(img, w, h, transparent)


def sheet(path, w, h, count, transparent=None, **kw):
    """Load a sprite sheet image file and cut it into `count` w x h Sprites,
    row-major from the top-left. Uses the F buffer as scratch (and overwrites
    it!) - load sheets before taking the scenery snapshot / showing sprites.
    Extra keyword args go to the image loader (e.g. dither for jpg/bmp)."""
    import pcimage
    made_f = False
    try:
        hdmi.create()
        made_f = True
    except ValueError:
        pass  # F already exists: use it as scratch (contents overwritten)
    prev = hdmi.write()
    hdmi.write("F")
    try:
        hdmi.fill(0)
        ext = path.rsplit(".", 1)[-1].lower()
        if ext == "png":
            pcimage.draw_png(path, **kw)
        elif ext in ("jpg", "jpeg"):
            pcimage.draw_jpg(path, **kw)
        else:
            pcimage.draw_bmp(path, **kw)
        per_row = hdmi.width() // w
        out = []
        for i in range(count):
            cx = (i % per_row) * w
            cy = (i // per_row) * h
            out.append(grab(cx, cy, w, h, transparent))
    finally:
        hdmi.write(prev)
        if made_f:
            hdmi.close("F")
    return out


def on_collision(cb=None):
    """Register cb(events) called from update() whenever new collisions
    happen; on_collision() removes it."""
    global _cb
    _cb = cb


def _setup():
    # Pick the compositing mode for the current video mode. RGB320: overlay
    # layer (scenery untouched). Others: F-buffer snapshot (dirty rectangles).
    global _mode, _ldisp, _erase, _gen, _all_dirty
    if hdmi.width() == 320:
        if hdmi.transparent() < 0:
            hdmi.layer()  # default transparent = black
        _erase = hdmi.transparent()
        prev = hdmi.write()
        hdmi.write("L")
        try:
            _ldisp = hdmi.fb()
        finally:
            hdmi.write(prev)
        _mode = "L"
    else:
        try:
            hdmi.create()
        except ValueError:
            pass  # F already exists: it becomes the scenery snapshot
        hdmi.copy("N", "F")
        _mode = "F"
    _gen = hdmi.gen()
    _all_dirty = True


def snapshot():
    """Re-capture the scenery after redrawing it ("F" mode; no-op on the
    layer, where the scenery is live). Call with the sprites' patches already
    erased - i.e. straight after drawing a fresh background."""
    if _mode == "F":
        hdmi.copy("N", "F")
    global _all_dirty
    _all_dirty = True


def reset():
    """Hide everything and forget walls/state (also called automatically
    after a screen-mode change)."""
    global _mode, _ldisp, _dirty, _all_dirty, _strip
    for s in _shown:
        s._sx = None
        s._touch.clear()
    del _shown[:]
    del _walls[:]
    _dirty = []
    _all_dirty = False
    _mode = None
    _ldisp = None
    _strip = None


def _isect(a, b):
    return not (a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0]
                or a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1])


def _erase_rect(r):
    if _mode == "L":
        _ldisp.fill_rect(r[0], r[1], r[2], r[3], _erase)
    else:
        hdmi.blit(r[0], r[1], r[2], r[3], r[0], r[1], "F", "N")


def _collisions(moved, events):
    # MMBasic ProcessCollisions semantics: AABB with touching edges counting
    # (sprites), layer partition (same layer, or either on layer 0), screen
    # edges, walls (strict overlap, as MMBasic static objects); all reported
    # edge-triggered - only NEW contacts create events.
    W = hdmi.width()
    H = hdmi.height()
    seen = set()
    for a in moved:
        if a not in _shown:
            continue
        contacts = set()
        for b in _shown:
            if b is a:
                continue
            if a.layer and b.layer and a.layer != b.layer:
                continue
            if not (b.x + b.w < a.x or b.x > a.x + a.w
                    or b.y + b.h < a.y or b.y > a.y + a.h):
                contacts.add(id(b))
                pair = (id(a) ^ id(b), min(id(a), id(b)))
                if id(b) not in a._touch and pair not in seen:
                    seen.add(pair)
                    events.append((a, b))
        if a.x < 0:
            contacts.add("left")
            if "left" not in a._touch:
                events.append((a, "left"))
        if a.y < 0:
            contacts.add("top")
            if "top" not in a._touch:
                events.append((a, "top"))
        if a.x + a.w > W:
            contacts.add("right")
            if "right" not in a._touch:
                events.append((a, "right"))
        if a.y + a.h > H:
            contacts.add("bottom")
            if "bottom" not in a._touch:
                events.append((a, "bottom"))
        for wl in _walls:
            if not (a.x + a.w <= wl.x or a.x >= wl.x + wl.w
                    or a.y + a.h <= wl.y or a.y >= wl.y + wl.h):
                contacts.add(id(wl))
                if id(wl) not in a._touch:
                    events.append((a, wl))
        a._touch = contacts


def update(vsync=False):
    """Commit all sprite changes to the screen in one pass and return the
    list of NEW collision events, each (sprite, other) where `other` is a
    Sprite, a Wall, or "left"/"right"/"top"/"bottom". vsync=True waits for
    vertical blanking first (also paces the loop to the refresh rate)."""
    global _all_dirty, _dirty
    if _mode is None or hdmi.gen() != _gen:
        if _mode is not None:
            reset()  # video mode changed under us: drop stale state
        _setup()
    if vsync:
        hdmi.vsync()
    # 1. Collect dirty rectangles (old + new) for every moved/new sprite.
    moved = []
    for s in _shown:
        if s._sx is None or s.x != s._sx or s.y != s._sy:
            moved.append(s)
            if s._sx is not None:
                _dirty.append((s._sx, s._sy, s.w, s.h))
            _dirty.append((s.x, s.y, s.w, s.h))
    if _all_dirty:
        _dirty = [(0, 0, hdmi.width(), hdmi.height())]
        moved = list(_shown)
    # 2. Erase the dirty patches (transparent fill on L / restore from F).
    for r in _dirty:
        _erase_rect(r)
    # 3. Redraw every sprite that intersects a dirty patch, in z-order.
    target = "L" if _mode == "L" else "N"
    for s in _shown:
        rect = (s.x, s.y, s.w, s.h)
        for r in _dirty:
            if _isect(rect, r):
                hdmi.blit(0, 0, s.w, s.h, s.x, s.y,
                          (s.img, s.w, s.h), target, s.transparent)
                break
        s._sx = s.x
        s._sy = s.y
    _dirty = []
    _all_dirty = False
    # 4. Collision pass over the sprites that moved (MMBasic semantics).
    events = []
    if moved:
        _collisions(moved, events)
    if events and _cb is not None:
        _cb(events)
    return events


def _scroll_axis(target, dx, W, H, horizontal, blank):
    # Shift one surface along one axis with wrap (or a blank fill colour).
    global _strip
    n = abs(dx)
    if horizontal:
        sw, sh = n, H
    else:
        sw, sh = W, n
    if blank is None:
        if hdmi.bpp() == 4 and horizontal and (n & 1):
            raise ValueError("horizontal scroll must be even in RGB1024")
        need = sw * sh * hdmi.bpp() // 8
        if _strip is None or len(_strip) < need:
            _strip = bytearray(need)
        strip = (_strip, sw, sh)
    if horizontal:
        if dx > 0:  # content moves right; right edge wraps to the left
            if blank is None:
                hdmi.blit(W - n, 0, n, H, 0, 0, target, strip)
            hdmi.blit(0, 0, W - n, H, n, 0, target, target)
            if blank is None:
                hdmi.blit(0, 0, n, H, 0, 0, strip, target)
        else:
            if blank is None:
                hdmi.blit(0, 0, n, H, 0, 0, target, strip)
            hdmi.blit(n, 0, W - n, H, 0, 0, target, target)
            if blank is None:
                hdmi.blit(0, 0, n, H, W - n, 0, strip, target)
    else:
        if dx > 0:  # content moves up; top edge wraps to the bottom
            if blank is None:
                hdmi.blit(0, 0, W, n, 0, 0, target, strip)
            hdmi.blit(0, n, W, H - n, 0, 0, target, target)
            if blank is None:
                hdmi.blit(0, 0, W, n, 0, H - n, strip, target)
        else:
            if blank is None:
                hdmi.blit(0, H - n, W, n, 0, 0, target, strip)
            hdmi.blit(0, 0, W, H - n, 0, n, target, target)
            if blank is None:
                hdmi.blit(0, 0, W, n, 0, 0, strip, target)
    if blank is not None:
        # Fill the exposed edge strip with the blank colour on the scrolled
        # surface (a Display bound to it gives a clipped C fill_rect).
        prev = hdmi.write()
        hdmi.write(target)
        try:
            fb = hdmi.fb()
            if horizontal:
                if dx > 0:
                    fb.fill_rect(0, 0, n, H, blank)
                else:
                    fb.fill_rect(W - n, 0, n, H, blank)
            else:
                if dx > 0:
                    fb.fill_rect(0, H - n, W, n, blank)
                else:
                    fb.fill_rect(0, 0, W, n, blank)
        finally:
            hdmi.write(prev)


def scroll(dx, dy, blank=None):
    """Scroll the scenery dx pixels right and dy pixels up (MMBasic SPRITE
    SCROLL): the image wraps around, or set `blank` (native colour) to fill
    the exposed edge instead. Layer-0 sprites and all walls move (and wrap)
    with the scenery; other layers stay put. Finishes with an update()."""
    global _all_dirty
    if _mode is None:
        _setup()
    W = hdmi.width()
    H = hdmi.height()
    if _mode == "F":
        # Sprites are painted INTO the scenery here: lift them first.
        for s in _shown:
            if s._sx is not None:
                hdmi.blit(s._sx, s._sy, s.w, s.h, s._sx, s._sy, "F", "N")
                s._sx = None
        surfaces = ("N", "F")
    else:
        surfaces = ("N",)  # sprites live on the layer; scenery is just N
    for t in surfaces:
        if dx:
            _scroll_axis(t, dx, W, H, True, blank)
        if dy:
            _scroll_axis(t, dy, W, H, False, blank)
    # Layer-0 sprites and walls travel with the scenery, wrapping at the
    # centre point exactly as MMBasic does.
    for s in _shown:
        if s.layer == 0:
            cx = (s.x + (s.w >> 1) + dx) % W
            cy = (s.y + (s.h >> 1) - dy) % H
            s.x = cx - (s.w >> 1)
            s.y = cy - (s.h >> 1)
    for wl in _walls:
        cx = (wl.x + (wl.w >> 1) + dx) % W
        cy = (wl.y + (wl.h >> 1) - dy) % H
        wl.x = cx - (wl.w >> 1)
        wl.y = cy - (wl.h >> 1)
    return update()
