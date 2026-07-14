# Tile maps for the Pico Computer 3 -- MMBasic's TILEMAP, as a Python class.
#
# A tile map is a grid of tile indices that reference a *tileset* image (a sheet
# of equal-size tiles). The heavy per-tile rendering is done in C by
# hdmi.tilemap(); this class holds the map, the viewport and the tile attributes
# and drives it. Tile index 0 is empty (nothing drawn); 1..N select tiles from
# the sheet in row-major order.
#
#     from pcimage import load_image
#     sheet = load_image("/sd/tiles.png")        # the tileset, in memory
#     tm = TileMap(sheet, 16, 16, cols=64, rows=32)   # 16x16 tiles, 64x32 map
#     tm.set(3, 5, 12)                            # cell (3,5) -> tile 12
#     tm.view(0, 0)
#     tm.draw()                                   # render the viewport to screen
#
# Scroll with tm.scroll(dx, dy) (or tm.view(x, y)) and redraw each frame. For
# flicker-free scrolling compose into the off-screen "F" buffer and copy it
# (see the manual). Game objects on the map are ordinary sprites (pcsprite) or
# tm.blit_tile(tile, x, y) for a one-off tile.

import array

import hdmi


class TileMap:
    def __init__(self, tileset, tile_w, tile_h, cols, rows, tiles_per_row=None, data=None):
        self.tileset = tileset          # a load_image() Image or a (buf, w, h) surface
        self.tw = tile_w
        self.th = tile_h
        self.cols = cols
        self.rows = rows
        _buf, sw, _sh = self._surface()
        self.tpr = tiles_per_row or (sw // tile_w)
        self.map = self._to_map(data, cols, rows)
        self.vx = 0
        self.vy = 0
        self.attrs = {}                 # tile index -> attribute value (default 0)

    def _surface(self):
        t = self.tileset
        s = getattr(t, "surface", None)
        if s is not None:               # a load_image() Image
            return s
        if hasattr(t, "buf"):
            return (t.buf, t.w, t.h)
        return t                        # already a (buf, w, h) tuple

    @staticmethod
    def _to_map(data, cols, rows):
        m = array.array("H", bytes(2 * cols * rows))   # all zeros
        if data is None:
            return m
        if data and isinstance(data[0], (list, tuple)):
            i = 0                        # a list of rows
            for row in data:
                for v in row:
                    m[i] = v
                    i += 1
        else:
            for i, v in enumerate(data): # a flat sequence
                m[i] = v
        return m

    # --- cell access ------------------------------------------------------
    def set(self, col, row, tile):
        self.map[col + row * self.cols] = tile

    def get(self, col, row):
        return self.map[col + row * self.cols]

    def fill(self, tile):
        for i in range(len(self.map)):
            self.map[i] = tile

    # --- viewport ---------------------------------------------------------
    def view(self, x, y):
        self.vx = x
        self.vy = y

    def scroll(self, dx, dy):
        self.vx += dx
        self.vy += dy

    def clamp(self, vw, vh):
        """Keep the viewport inside the world so it can't scroll past an edge
        (call with the viewport size you draw with)."""
        self.vx = max(0, min(self.vx, self.cols * self.tw - vw))
        self.vy = max(0, min(self.vy, self.rows * self.th - vh))

    # --- render -----------------------------------------------------------
    def draw(self, sx=0, sy=0, vw=None, vh=None, skip=-1, dst=None):
        """Render the viewport (world pixel vx,vy; size vw x vh) to `dst` at
        screen (sx,sy). Defaults to the whole screen. `skip` is a transparent
        colour (native format), -1 = opaque."""
        if vw is None:
            vw = hdmi.width()
        if vh is None:
            vh = hdmi.height()
        hdmi.tilemap(self.map, self.cols, self.rows, self._surface(), self.tpr,
                     self.tw, self.th, self.vx, self.vy, sx, sy, vw, vh, skip, dst)

    def blit_tile(self, tile, x, y, skip=-1, dst=None):
        """Draw a single tile from the sheet at screen pixel (x,y) -- handy for a
        player/object without a full sprite. Tile 0 draws nothing."""
        if tile <= 0:
            return
        sx = ((tile - 1) % self.tpr) * self.tw
        sy = ((tile - 1) // self.tpr) * self.th
        hdmi.blit(sx, sy, self.tw, self.th, x, y, self._surface(), dst, skip)

    # --- attributes / collision ------------------------------------------
    def set_attr(self, tile, value):
        """Tag a tile index with an attribute value (e.g. a bitmask of flags
        like 'solid'). Read with attr()/attr_at(); test regions with collide()."""
        self.attrs[tile] = value

    def attr(self, tile):
        return self.attrs.get(tile, 0)

    def tile_at(self, wx, wy):
        """The tile index at world pixel (wx, wy), or 0 if outside the map."""
        c = wx // self.tw
        r = wy // self.th
        if 0 <= c < self.cols and 0 <= r < self.rows:
            return self.map[c + r * self.cols]
        return 0

    def attr_at(self, wx, wy):
        return self.attrs.get(self.tile_at(wx, wy), 0)

    def collide(self, wx, wy, w=1, h=1, mask=None):
        """True if the world rectangle (wx,wy,w,h) overlaps a 'solid' tile.
        Solid means non-empty by default, or `attr & mask` when `mask` is given
        (so you can flag only some tiles as blocking)."""
        c0 = wx // self.tw
        r0 = wy // self.th
        c1 = (wx + w - 1) // self.tw
        r1 = (wy + h - 1) // self.th
        for r in range(r0, r1 + 1):
            if not (0 <= r < self.rows):
                continue
            base = r * self.cols
            for c in range(c0, c1 + 1):
                if not (0 <= c < self.cols):
                    continue
                t = self.map[base + c]
                if t and (mask is None or (self.attrs.get(t, 0) & mask)):
                    return True
        return False

    # --- loading ----------------------------------------------------------
    @staticmethod
    def load(path):
        """Read a map file into a list of rows of ints (comma- or space-
        separated, one map row per line; blank lines and # comments ignored).
        Pass the result as `data=` to TileMap()."""
        rows = []
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                rows.append([int(p) for p in line.replace(",", " ").split()])
        return rows
