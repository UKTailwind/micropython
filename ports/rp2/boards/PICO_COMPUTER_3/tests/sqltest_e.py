# usqlite fix validation - PART E: power-fail checker.
# Run after a power cut during sqltest_d. Verifies the database recovered:
# integrity intact and no torn batch (count(*) must equal the ledger).
import usqlite

DB = "/powertest.db"

db = usqlite.connect(DB)   # hot-journal rollback happens on first access
ic = db.execute("PRAGMA integrity_check").fetchone()[0]
rows = db.execute("SELECT COUNT(*) FROM batches").fetchone()[0]
ledger = db.execute("SELECT count FROM ledger").fetchone()[0]
seq = db.execute(
    "SELECT COUNT(*) FROM batches WHERE n >= (SELECT count FROM ledger)"
).fetchone()[0]
db.close()

print("integrity_check:", ic)
print("rows:", rows, "ledger:", ledger, "rows-past-ledger:", seq)
if ic == "ok" and rows == ledger and seq == 0:
    print("POWER-FAIL CHECK: ok -- database recovered cleanly")
else:
    print("POWER-FAIL CHECK: FAIL")
