import usqlite

DB = "/sd/scores.db"

def open_db():
    con = usqlite.connect(DB)
    con.execute("CREATE TABLE IF NOT EXISTS"
                " scores(name TEXT, score INTEGER)")
    return con

def add(con, name, score):
    con.execute("INSERT INTO scores VALUES (?, ?)",
                (name, score))

def top(con, n=10):
    return con.execute(
        "SELECT name, score FROM scores"
        " ORDER BY score DESC LIMIT ?", (n,)).fetchall()

con = open_db()
name = input("Name: ")
score = int(input("Score: "))
add(con, name, score)

print("--- HIGH SCORES ---")
for i, (nm, sc) in enumerate(top(con), 1):
    print("%2d. %-8s %6d" % (i, nm, sc))
con.close()
