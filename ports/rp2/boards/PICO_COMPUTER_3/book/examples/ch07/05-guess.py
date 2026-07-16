import random

secret = random.randint(1, 5)

print("I have chosen a number from 1 to 5.")
guess = int(input("One guess. Steel yourself: "))

if guess == secret:
    beep()
    print("INCREDIBLE. That was it exactly.")
elif abs(guess - secret) == 1:
    print(f"Agonising -- you were one away. It was {secret}.")
elif guess < secret:
    print(f"Too low. It was {secret}.")
else:
    print(f"Too high. It was {secret}.")
