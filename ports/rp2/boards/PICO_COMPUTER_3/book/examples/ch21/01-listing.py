keydown()      # or keydown(0): how many keys are held (0-6)
keydown(1)     # code of the most recent key held (0 = none)
keydown(2)     # ...the one before it, up to keydown(6)
# modifier bitmap: Shift/Ctrl/Alt/GUI, left and right
keydown(7)
keydown(8)     # lock bitmap: Caps=1, Num=2, Scroll=4
