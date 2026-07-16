class Hero:
    def __init__(self, name, health=20):
        self.name = name
        self.health = health
        self.gold = 0

    def take_damage(self, amount):
        self.health -= amount
        if self.health <= 0:
            self.health = 0
            print(f"{self.name} has been defeated!")

    def drink_potion(self):
        self.health += 10
        print(f"{self.name} feels much better.")

    def is_alive(self):
        return self.health > 0
