scores = []

print("HIGH SCORE TABLE -- type a name, or just Enter to finish")

while True:
    name = input("Name: ")
    if name == "":
        break
    points = int(input(f"Score for {name}: "))
    scores.append((points, name))

scores.sort(reverse=True)

print()
print("=" * 24)
print("   HALL OF FAME")
print("=" * 24)
for i, (points, name) in enumerate(scores[:5]):
    print(f"{i + 1}. {name:12} {points:5}")
