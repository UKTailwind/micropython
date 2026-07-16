import random

class Hero:
    def __init__(self, name, health=20):
        self.name = name
        self.health = health

    def take_damage(self, amount):
        self.health -= amount
        if self.health < 0:
            self.health = 0

    def is_alive(self):
        return self.health > 0

    def __str__(self):
        return f"{self.name:20} {'#' * self.health}"

ada = Hero("Ada the Adequate")
grumble = Hero("Grumble the Goblin", 16)

print("A DUEL COMMENCES\n")
fighters = [ada, grumble]

while ada.is_alive() and grumble.is_alive():
    attacker, defender = fighters
    blow = random.randint(1, 6)
    defender.take_damage(blow)
    print(f"{attacker.name} strikes for {blow}!")
    print(defender)
    fighters = [defender, attacker]      # your turn now

winner = ada if ada.is_alive() else grumble
print(f"\n{winner.name} is victorious!")
