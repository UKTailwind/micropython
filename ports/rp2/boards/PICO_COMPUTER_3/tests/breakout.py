# Breakout -- a tile-map brick breaker, ported from MMBasic's breakout.bas to
# exercise the new tile-map engine (TileMap + hdmi.tilemap). The brick field is
# a tile map; tile attributes drive brick/wall collision; the ball and paddle
# are drawn with tm.blit_tile(). The tileset is built in memory (no asset file).
#
# Controls (USB keyboard): Left/Right arrows move the paddle, Space launches the
# ball, Q or Esc quits.
#
#   run("/sd/tests/breakout.py")

import math
import time

import framebuf
import hdmi
import keyboard
import pcgfx
from pctilemap import TileMap

SCR_W, SCR_H = 320, 240
TW, TH = 16, 8            # tiles are wide and short (bricks)
TPR = 8                  # tiles per row in the sheet
COLS, ROWS = 20, 30      # 20*16 = 320, 30*8 = 240 -> exactly the screen

# tile indices (1-based in the map; tile N is sheet cell N-1). 0 = empty.
T_EMPTY, T_RED, T_YELLOW, T_GREEN, T_BLUE, T_WALL, T_BALL, T_PADDLE = range(8)
POINTS = {T_RED: 7, T_YELLOW: 5, T_GREEN: 3, T_BLUE: 1}
A_BRICK, A_WALL = 1, 2   # attribute bits

PAD_W_TILES = 5
PAD_ROW = 28
HUD_ROWS = 2             # top rows kept clear (black) for the score/lives HUD
TOP_Y = (HUD_ROWS + 1) * TH   # ball bounces here -- just below the top wall
BRICK_ROW0 = 4
BRICK_ROWS = 8
QUIT = (ord("q"), ord("Q"), 27)
SPACE = 32


def _tileset():
    """Build the tile sheet (RGB565) and return a (buf, w, h) surface. Tile N is
    drawn at sheet cell N-1 (matching hdmi.tilemap's 1-based indexing)."""
    sw, sh = TPR * TW, TH
    buf = bytearray(sw * sh * 2)
    d = pcgfx.Display(buf, sw, sh, framebuf.RGB565)

    def tile(idx, rgb, outline=True):
        x = (idx - 1) * TW
        d.fill_rect(x, 0, TW, TH, d.colour(rgb))
        if outline:
            d.rect(x + 1, 1, TW - 2, TH - 2, d.colour(0xFFFFFF))
        return x

    tile(T_RED, 0xFF0000)
    tile(T_YELLOW, 0xFFFF00)
    tile(T_GREEN, 0x00FF00)
    tile(T_BLUE, 0x0000FF)
    tile(T_WALL, 0x808080)
    x = tile(T_BALL, 0x000000, outline=False)     # black bg = transparent
    d.fill_rect(x + TW // 2 - 3, TH // 2 - 3, 6, 6, d.colour(0xFFFFFF))
    tile(T_PADDLE, 0x00FFFF)
    return (buf, sw, sh)


def _build_map(tm):
    colours = (T_RED, T_RED, T_YELLOW, T_YELLOW, T_GREEN, T_GREEN, T_BLUE, T_BLUE)
    for r in range(ROWS):
        for c in range(COLS):
            if r < HUD_ROWS:
                tm.set(c, r, T_EMPTY)                # black HUD band at the top
            elif c == 0 or c == COLS - 1 or r == HUD_ROWS:
                tm.set(c, r, T_WALL)                 # side walls + top wall
            elif BRICK_ROW0 <= r < BRICK_ROW0 + BRICK_ROWS:
                tm.set(c, r, colours[r - BRICK_ROW0])
            else:
                tm.set(c, r, T_EMPTY)


def _count_bricks(tm):
    return sum(1 for t in tm.map if tm.attr(t) & A_BRICK)


def _held(code):
    for i in range(1, keyboard.keydown(0) + 1):
        if keyboard.keydown(i) == code:
            return True
    return False


def _held_any(codes):
    for i in range(1, keyboard.keydown(0) + 1):
        if keyboard.keydown(i) in codes:
            return True
    return False


def _wait_key(codes):
    while keyboard.keydown(0):            # wait for release (keys auto-repeat)
        time.sleep_ms(10)
    while True:
        for i in range(1, keyboard.keydown(0) + 1):
            k = keyboard.keydown(i)
            if k in codes:
                return k
        time.sleep_ms(15)


def run():
    import pcconsole

    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock
    pcconsole.console("serial")
    fb = hdmi.fb()
    C = fb.colour
    black = C(0x000000)

    sheet = _tileset()
    try:
        hdmi.create()                      # off-screen F buffer
    except ValueError:
        pass

    def centre(s, y, rgb, scale=1):
        hdmi.text(s, SCR_W // 2 - (len(s) * 8 * scale) // 2, y, C(rgb), -1, scale, 1)

    def splash(line1, line2):
        hdmi.write("F")
        hdmi.fill(black)
        centre("BREAKOUT", 60, 0xFF0000, 3)
        centre(line1, 130, 0xFFFFFF)
        centre(line2, 160, 0xFFFF00)
        hdmi.write("N")
        hdmi.vsync()
        hdmi.copy("F", "N")

    try:
        while True:
            splash("Arrows move,  Space launches", "Space to start,  Q to quit")
            if _wait_key(QUIT + (SPACE,)) in QUIT:
                return

            score, lives, level, speed = 0, 3, 1, 4.0
            tm = TileMap(sheet, TW, TH, COLS, ROWS, tiles_per_row=TPR)
            for t in (T_RED, T_YELLOW, T_GREEN, T_BLUE):
                tm.set_attr(t, A_BRICK)
            tm.set_attr(T_WALL, A_WALL)

            running = True
            while running:                         # one life-set of levels
                _build_map(tm)
                bricks = _count_bricks(tm)
                pad_x = (SCR_W - PAD_W_TILES * TW) // 2
                launched = False
                ball_x = pad_x + (PAD_W_TILES * TW) // 2 - TW // 2
                ball_y = (PAD_ROW - 1) * TH
                ball_dx, ball_dy = speed, -speed

                level_active = True
                while level_active:
                    # ---- input ----
                    if _held(keyboard.LEFT):
                        pad_x = max(TW, pad_x - 6)
                    if _held(keyboard.RIGHT):
                        pad_x = min(SCR_W - PAD_W_TILES * TW - TW, pad_x + 6)
                    if _held(SPACE):
                        launched = True
                    if _held_any(QUIT):
                        return

                    # ---- ball ----
                    if not launched:
                        ball_x = pad_x + (PAD_W_TILES * TW) // 2 - TW // 2
                        ball_y = (PAD_ROW - 1) * TH
                    else:
                        nx = ball_x + ball_dx
                        ny = ball_y + ball_dy
                        if nx < TW:
                            nx, ball_dx = TW, -ball_dx
                        if nx > SCR_W - 2 * TW:
                            nx, ball_dx = SCR_W - 2 * TW, -ball_dx
                        if ny < TOP_Y:
                            ny, ball_dy = TOP_Y, -ball_dy

                        bx, by = int(nx) + TW // 2, int(ny) + TH // 2
                        pcol = (int(ball_x) + TW // 2) // TW
                        prow = (int(ball_y) + TH // 2) // TH
                        hit = tm.tile_at(bx, by)
                        if hit:
                            a = tm.attr(hit)
                            tcol, trow = bx // TW, by // TH
                            if a & A_BRICK:
                                score += POINTS.get(hit, 0)
                                tm.set(tcol, trow, T_EMPTY)
                                bricks -= 1
                            if a & (A_BRICK | A_WALL):
                                if pcol != tcol:
                                    ball_dx = -ball_dx
                                if prow != trow or pcol == tcol:
                                    ball_dy = -ball_dy

                        # paddle (only while descending)
                        if ball_dy > 0:
                            top = PAD_ROW * TH
                            if top <= int(ny) + TH <= top + TH and \
                               int(nx) + TW > pad_x and int(nx) < pad_x + PAD_W_TILES * TW:
                                ny = top - TH
                                pos = (nx + TW // 2 - pad_x) / (PAD_W_TILES * TW)
                                ball_dx = (pos - 0.5) * speed * 2
                                lim = speed * 0.9
                                if abs(ball_dx) > lim:
                                    ball_dx = lim if ball_dx > 0 else -lim
                                if abs(ball_dx) < 0.3:
                                    ball_dx = 0.3 if ball_dx >= 0 else -0.3
                                ball_dy = -math.sqrt(max(0.0, speed * speed - ball_dx * ball_dx))

                        if ny > SCR_H:                       # ball lost
                            lives -= 1
                            if lives <= 0:
                                level_active = running = False
                                break
                            launched = False
                            ball_dx, ball_dy = speed, -speed
                            continue
                        ball_x, ball_y = nx, ny

                    # ---- draw frame ----
                    hdmi.write("F")
                    hdmi.fill(black)
                    tm.draw()
                    for ps in range(PAD_W_TILES):
                        tm.blit_tile(T_PADDLE, pad_x + ps * TW, PAD_ROW * TH, skip=black)
                    tm.blit_tile(T_BALL, int(ball_x), int(ball_y), skip=black)
                    hdmi.text("SCORE:%d" % score, 4, 0, C(0xFFFFFF))
                    hdmi.text("LVL:%d" % level, SCR_W // 2 - 24, 0, C(0xFFFF00))
                    lv = "LIVES:%d" % lives
                    hdmi.text(lv, SCR_W - 4 - len(lv) * 8, 0, C(0xFFFFFF))
                    hdmi.write("N")
                    hdmi.vsync()
                    hdmi.copy("F", "N")

                    if bricks <= 0:                          # level cleared
                        level += 1
                        speed *= 1.25
                        level_active = False                 # rebuild next level

            splash("Score %d   Level %d" % (score, level), "Space to play again,  Q to quit")
            if _wait_key(QUIT + (SPACE,)) in QUIT:
                return
    finally:
        hdmi.write("N")
        pcconsole.console("both")
        import testutil as T

        T.restore_screen()


if __name__ == "__main__":
    run()
