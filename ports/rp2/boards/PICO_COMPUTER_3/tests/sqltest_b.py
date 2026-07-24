# usqlite fix validation - PART B (run AFTER the Ctrl-D soft reset)
import usqlite, gc, os
print("=== SQLTEST B (post soft reset) ===")
db = usqlite.connect("/sqltest.db")
row = db.execute("SELECT COUNT(*), SUM(v) FROM t").fetchone()
print("B1 count/sum:", row, "ok" if row == (100, 7425.0) else "FAIL")
for k in range(5):
    x = ["pad%d" % i for i in range(2000)]
    rows = db.execute("SELECT * FROM t ORDER BY v DESC").fetchall()
    assert len(rows) == 100 and rows[0][2] == 148.5
    gc.collect()
print("B2 post-reset churn: ok, mem:", usqlite.mem_current())
print("B3 closing...")
db.close()
print("B3 closed: ok")

# fix 3: root-level db name while cwd is elsewhere
os.chdir("/sd" if "sd" in os.listdir("/") else "/lib")
print("B4 cwd:", os.getcwd())
db = usqlite.connect("/sqltest.db")
n = db.execute("SELECT COUNT(*) FROM t").fetchone()[0]
db.close()
os.chdir("/")
print("B4 root-path count:", n, "ok" if n == 100 else "FAIL")

os.remove("/sqltest.db")
print("=== PART B DONE - if no FAIL above, all five fixes pass on hardware ===")
