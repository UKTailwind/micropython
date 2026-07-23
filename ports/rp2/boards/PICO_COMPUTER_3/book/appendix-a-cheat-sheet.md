# Appendix A — The cheat sheet

The daily 95%, one appendix. Keep it beside the keyboard until it's in
your fingers.

## The shell (chapter 4)

```
ls()  ls("/sd/*.py")        list (wildcards)
cls()                       clear screen
cd("/sd")  pwd()            change / show directory
cat("f.py")                 view, paged (q stops)
cp("a.py", "b.py")          copy    (wildcards -> dir)
mv("old", "new")            move / rename
rm("f.tmp")                 delete -- NO undo
mkdir("d")  rmdir("d")      make / remove directory
run("prog.py")              run a program
edit("prog.py")             full-screen editor
autosave("prog.py")         paste code in, Ctrl-Z ends
fm()                        file manager (below)
xrecv("f") / xsend("f")     XMODEM over USB-C serial
```

## `fm` keys (chapter 4)

Tab/←/→ panes · ↑/↓ move · Enter opens *by type* (py runs, music
plays, images show, text views) · Backspace up · **Space select
several** · C copy / M move to other pane · R rename · D delete ·
N new dir · E edit · S stop audio · Q quit.

## The editor, `pye` (chapter 5)

```
Ctrl-S save        Ctrl-Q quit (y/N/f)     Ctrl-Z/Y undo/redo
Ctrl-F find        Ctrl-N find next        Ctrl-G  go to line
# comment
Tab / Shift-Tab    indent / unindent       Ctrl-P  toggle
Ctrl-L mark, then  Ctrl-C/X/V copy/cut/paste
```

## The prompt (chapters 3, 8)

- ↑/↓ history · Tab completes names · `help(math)` lists a module.
- **Blocks**: after a `:` line the prompt shows `...` and auto-indents.
  To finish: **Backspace (eats one indent level), then Enter.**
- **One entry, one statement** — run the block first, then type the
  next command at the fresh `>>>`.
- Ctrl-C interrupts · Ctrl-D soft-restarts (re-reads edited libraries).

## Screen (chapters 15–17)

```
screen(hdmi.RGB640)             set + save mode (RGB320/512/1024)
d = hdmi.fb()                   Display -- AFTER hdmi.write()!
d.colour(0xRRGGBB)              convert colour FIRST, always
d.fill(c)  d.pixel(x,y,c)  d.line(x1,y1,x2,y2,c,w)
d.rect / fill_rect(x,y,w,h,c)   d.ellipse(x,y,rx,ry,c,True)
d.rbox(x,y,w,h,r,c[,fill]) d.arc(x,y,r1,r2,a1,a2,c) d.flood(x,y,c)
hdmi.text(s,x,y,fg,bg,scale,font)  fonts 1..9; 6=digits; bg=-1
draw_jpg/png/bmp(path)          save_image(path)
sheet = load_image(path, transparent=c)
sheet.cell(col,row,cw,ch,x,y,skip=c)
# flip pattern:
hdmi.close("F"); hdmi.create(); hdmi.write("F")
# then finally write("N")
...draw frame... ; hdmi.vsync(); hdmi.copy("F","N")
console("none")                 hide it; console() shows again
```

## Input (chapters 18, 21)

```
keydown(0/1..6/7/8)     count / keys held / modifiers / locks
keyboard.UP DOWN LEFT RIGHT ESC ENTER F1..F12
keyboard.on_key(fn)     event per press; on_key() removes
mouse("X"/"Y"/"L"/"R"/"W"/"D")      pccursor.on()/refresh()/off()
touch("X")  -1 = nothing    touch("DOWN"/"TAP"/"SWL"...)  latched
```

## Sound (chapter 20)

```
volume(60)   beep(880, 150)   play("/sd/f.mp3" [, loop=True])
pause() resume() stop() is_playing()
tone(freqL[, freqR, ms, wait=True])          retunes live
sound(voice1-4, "L/R/B", "S/Q/T/W/P/N/O", freq, vol0-25)
mod_sample(n[, rate=])                        SFX over .mod music
```

## Games (chapters 18, 19, 22)

```
# speeds * dt!
clock = pcgame.Clock(vsync=True); dt = clock.tick()
import pcsprite as sp
s = sp.grab(x,y,w,h,transparent=c); s.show(x,y,layer=n); s.x += 1
# collisions; sp.reset() when done
for a, b in sp.update(vsync=True): ...
tm = TileMap(sheet, 16, 16, cols, rows)
tm.set(c,r,t); tm.view(x,y); tm.draw()
tm.set_attr(tile, 1); tm.collide(x, y, w, h, mask=1)
```

## Time & system (chapters 29, 36)

```
gettime() -> tuple   settime(y,mo,d,h,mi,s)   ntpsync() tz(1)
time.ticks_ms() ticks_us() ticks_diff(now,t0) sleep_ms(n)
ds3231.set_alarm(h, m)  alarm_fired()  clear_alarm()  alarm_pin()
machine.Timer(period=500, callback=f)  pin.irq(f, Pin.IRQ_FALLING)
console("both"/"serial"/"screen"/"none")  keymap("UK")
rm("/settings.json") = factory reset;  /boot.py then /main.py
```

## GPIO (chapter 32)

```
p = Pin(0, Pin.OUT); p.on()/off()/toggle()
b = Pin(1, Pin.IN, Pin.PULL_UP)      pressed reads 0
machine.ADC(Pin(40)).read_u16()      analogue: GP40-46
pwm = machine.PWM(Pin(2), freq=50); pwm.duty_u16(n)
machine.I2C(0, sda=Pin(20), scl=Pin(21)).scan()      QWIIC bus
```

## The collars (never forgotten)

```python
try:
    ...                      # anything using write("F"/"L"),
# sprites, GUI (g.stop()), irq handlers,
finally:
    hdmi.write("N")          # mode changes, music (stop())
    console()
```
