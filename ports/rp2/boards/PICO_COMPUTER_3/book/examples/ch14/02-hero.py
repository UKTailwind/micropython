class Hero:
    def __init__(self, name, health=20):
        self.name = name
        self.health = health
        self.gold = 0

h = Hero("Ada")
g = Hero("Grumble the Goblin", health=8)

print(h.name, h.health, h.gold)
print(g.name, g.health, g.gold)
