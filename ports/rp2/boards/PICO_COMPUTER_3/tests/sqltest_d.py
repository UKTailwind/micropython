# usqlite fix validation - PART D: power-fail writer.
# Run it, let it print a few batches, then CUT THE POWER mid-run.
# After power-up, run("sqltest_e.py") to verify the database survived.
# Change DB to "/sd/powertest.db" to test the SD card instead of flash.
import usqlite

DB = "/powertest.db"

db = usqlite.connect(DB)
db.execute("CREATE TABLE IF NOT EXISTS batches (n INTEGER PRIMARY KEY, pad TEXT)")
db.execute("CREATE TABLE IF NOT EXISTS ledger (id INTEGER PRIMARY KEY, count INTEGER)")
db.execute("INSERT OR IGNORE INTO ledger VALUES (1, 0)")

start = db.execute("SELECT count FROM ledger").fetchone()[0]
print("starting at ledger count", start)
print(">>> PULL THE POWER whenever you like <<<")

n = start
while True:
    # one atomic unit: 20 rows + the ledger update. After any power cut the
    # checker must find count(*) == ledger.count exactly -- no torn batches.
    db.execute("BEGIN")
    for i in range(20):
        db.execute("INSERT INTO batches VALUES (?, ?)", (n + i, "x" * 100))
    db.execute("UPDATE ledger SET count = count + 20 WHERE id = 1")
    db.execute("COMMIT")
    n += 20
    print("committed batch, ledger =", n)
