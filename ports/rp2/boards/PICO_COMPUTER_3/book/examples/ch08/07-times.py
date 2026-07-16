import random

score = 0
print("TIMES-TABLE DOJO -- 5 questions. Begin!")

for q in range(5):
    a = random.randint(2, 12)
    b = random.randint(2, 12)
    answer = int(input(f"Q{q + 1}: what is {a} x {b}? "))
    if answer == a * b:
        score += 1
        print("Correct!")
    else:
        print(f"No -- {a} x {b} is {a * b}.")

print(f"Final score: {score} out of 5.")
if score == 5:
    beep()
    print("Flawless. The dojo bows to you.")
elif score >= 3:
    print("Solid. Return tomorrow.")
else:
    print("The dojo suggests... practice.")
