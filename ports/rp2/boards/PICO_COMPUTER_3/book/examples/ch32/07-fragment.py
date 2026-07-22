            if held(keyboard.LEFT):
                px -= 420 * dt
            if held(keyboard.RIGHT):
                px += 420 * dt
            px = max(0, min(W - PW, px))
