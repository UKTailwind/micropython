def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False
