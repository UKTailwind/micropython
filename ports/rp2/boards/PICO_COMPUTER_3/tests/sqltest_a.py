# usqlite fix validation - PART A (run in a fresh session)
import usqlite, gc, os
print("=== SQLTEST A ===")
try:
    os.remove("/sqltest.db")
except OSError:
    pass
db = usqlite.connect("/sqltest.db")
db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, v REAL)")
db.execute("BEGIN")
for i in range(100):
    db.execute("INSERT INTO t (name, v) VALUES (?, ?)", ("row%d" % i, i * 1.5))
db.execute("COMMIT")
assert db.execute("SELECT COUNT(*) FROM t").fetchone()[0] == 100
print("A1 basic populate: ok")

gc.collect()
base = usqlite.mem_current()
for i in range(30):
    try:
        db.executemany("INSERT INTO nosuch VALUES (1)")
    except usqlite.usqlite_Error:
        pass
leak = usqlite.mem_current() - base
print("A2 executemany 30-error leak bytes:", leak, "ok" if leak < 512 else "FAIL")

c1 = db.execute("SELECT * FROM t")
c2 = db.execute("SELECT * FROM t")
c2.close()
db.close()
gc.collect()
for i in range(2000):
    x = [i] * 8
ok = c1.fetchone() is None and list(c1) == []
print("A3 close-with-live-cursors:", "ok" if ok else "FAIL")

db2 = usqlite.connect("/sqltest.db")
db2.execute("SELECT COUNT(*) FROM t").fetchone()
mid = usqlite.mem_current()
db2 = None
gc.collect()
after = usqlite.mem_current()
print("A4 finaliser mem mid/after:", mid, after, "ok" if after < mid else "FAIL")

# leave a connection OPEN across the coming soft reset (worst case for fix 1)
dbopen = usqlite.connect("/sqltest.db")
dbopen.execute("SELECT COUNT(*) FROM t").fetchone()
print("A5 connection deliberately left open")
print("=== PART A DONE ===")
print("Now press Ctrl-D (soft reset), then: run('sqltest_b.py')")
