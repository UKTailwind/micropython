questions = [
    ("What does the M in MMBasic stand for?", "maximite"),
    ("What chip powers the Pico Computer 3?", "rp2350b"),
    ("Home is the screen's... (top-left or centre)", "centre"),
    ("What key saves in the editor? (ctrl-...)", "ctrl-s"),
]

score = 0
for q, a in questions:
    answer = input(q + " ")
    if answer == a:
        score += 1
        print("Correct!")
    else:
        print(f"It was: {a}")

print(f"\nYou scored {score} out of {len(questions)}.")
