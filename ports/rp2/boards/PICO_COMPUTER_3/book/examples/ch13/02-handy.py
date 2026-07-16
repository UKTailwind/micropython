# handy.py -- input that survives humans.

def ask_int(prompt):
    """Ask until the reply is a whole number; return it."""
    while True:
        reply = input(prompt).strip()
        try:
            return int(reply)
        except ValueError:
            print(f"'{reply}' isn't a whole number -- try again.")
