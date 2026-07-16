import random

secret = random.randint(1, 100)
tries = 0
guess = 0

print("I have chosen a number from 1 to 100.")

while guess != secret:
    guess = int(input("Your guess: "))
    tries += 1
    if guess < secret:
        print("Higher...")
    elif guess > secret:
        print("Lower...")

beep()
print(f"Got it in {tries} tries!")
if tries <= 7:
    print("Seven or fewer. Champion form.")
