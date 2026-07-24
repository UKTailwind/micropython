# handy.py -- input that survives humans.

def ask_int(prompt):
    """Ask until the reply is a whole number; return it."""
    while True:
        reply = input(prompt).strip()
        try:
            return int(reply)
        except ValueError:
            print(f"'{reply}' isn't a whole number -- try again.")


# held() -- chapter 18's keyboard helper, given its
# permanent home here as chapter 21 instructs.
from keyboard import keydown

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False
