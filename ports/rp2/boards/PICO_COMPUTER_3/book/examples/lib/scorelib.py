# scorelib.py -- persistent high scores.
#   import scorelib
#   scorelib.add(120, "Ada"); scorelib.show()

FILE = "/scores.csv"

def load():
    """Return the saved list of (points, name), best first."""
    scores = []
    try:
        with open(FILE) as f:
            for line in f:
                points, name = line.strip().split(",")
                scores.append((int(points), name))
    except OSError:
        # no file yet: first ever run -- empty list
        pass
    scores.sort(reverse=True)
    return scores

def save(scores):
    with open(FILE, "w") as f:
        for points, name in scores:
            f.write(f"{points},{name}\n")

def add(points, name):
    """Record a result. Returns its rank (1 = a new best)."""
    scores = load()
    scores.append((points, name))
    scores.sort(reverse=True)
    save(scores)
    return scores.index((points, name)) + 1

def show(top=5):
    print("=" * 24)
    print("   HALL OF FAME")
    print("=" * 24)
    for i, (points, name) in enumerate(load()[:top]):
        print(f"{i + 1}. {name:12} {points:5}")
