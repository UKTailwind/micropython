import time

print("PICO COMPUTER 3 -- PERSONALITY MODULE v0.1")
time.sleep(1)

name = input("What is your name? ")
print(f"{name}! Superb name. {len(name)} characters of pure "
      f"style.")

age = int(input(f"And how old are you, {name}? "))
days = age * 365
print(f"So you're roughly {days} days old. I'm told that's "
      f"{age * 7} in dog years.")

colour = input("Last one: favourite colour? ")
print(f"Noted. A {colour} spaceship it is.")
time.sleep(1)

beep()
print(f"Delighted to meet you, {name}. Press up-arrow any time "
      f"you miss me.")
