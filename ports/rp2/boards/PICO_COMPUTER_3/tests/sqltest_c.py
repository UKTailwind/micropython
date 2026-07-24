# usqlite fix validation - PART C: fixes 4 (64-bit int), 5 (description), 6 (VFS errors)
import usqlite, os
print("=== SQLTEST C ===")
try:
    os.remove("/t456.db")
except OSError:
    pass
db = usqlite.connect("/t456.db")

db.execute("CREATE TABLE big (id INTEGER PRIMARY KEY, v INTEGER)")
vals = [2**40, 1753305600123, -2**45, 2**62, -1, 0, 2**31, -(2**31) - 1]
for i, v in enumerate(vals):
    db.execute("INSERT INTO big VALUES (?, ?)", (i, v))
back = [r[0] for r in db.execute("SELECT v FROM big ORDER BY id")]
s = db.execute("SELECT SUM(v) FROM big").fetchone()[0]
print("C1 64-bit roundtrip:", "ok" if (back == vals and s == sum(vals)) else "FAIL", back[1])

cur = db.cursor()
cur.execute("SELECT COUNT(*), v + 1, v FROM big")
d = cur.description
ok5 = len(d) == 3 and d[0][1] is None and d[2][1] == "INTEGER"
cur.fetchall()
cur2 = db.execute("SELECT id, v FROM big")
d2 = cur2.description
ok5 = ok5 and len(d2) == 2 and d2[0][0] == "id"
cur2.fetchall()
print("C2 description/decltype:", "ok" if ok5 else "FAIL")

try:
    usqlite.connect("/nosuchdir/x.db")
    print("C3 bad-path connect: FAIL (no error)")
except usqlite.usqlite_Error as e:
    print("C3 bad-path connect -> usqlite_Error: ok")
n = db.execute("SELECT COUNT(*) FROM big").fetchone()[0]
print("C3 engine still usable:", "ok" if n == len(vals) else "FAIL")

def bad_trace(stmt):
    raise ValueError("boom")
db.set_trace_callback(bad_trace)
n = db.execute("SELECT COUNT(*) FROM big").fetchone()[0]
db.set_trace_callback(None)
print("C4 raising trace callback:", "ok" if n == len(vals) else "FAIL")

db.close()
os.remove("/t456.db")
print("=== PART C DONE - all ok above means fixes 4/5/6 pass on hardware ===")
