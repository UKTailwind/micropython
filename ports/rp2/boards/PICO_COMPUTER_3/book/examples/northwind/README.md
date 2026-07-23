# Northwind Traders — an on-device database demo

A small approximation of Microsoft's classic **Northwind** sample database,
built to show off two of the Pico Computer 3's headline features working
together: the on-device **SQLite** engine (`usqlite`) and the **`pcgui`**
point-and-click toolkit.

Pick a customer, drill into their orders and line items, and run
whole-database reports — all driven by real SQL joins and `GROUP BY`
aggregates, not hand-rolled loops.

## Files

| File | What it is |
|---|---|
| `nwdata.py` | The schema (8 tables) and seed data, plus `build()` / `open_db()`. Pure Python data — imports on a desktop too. |
| `nwquery.py` | The query library: every join and aggregate the UI uses, one function each, all parameterised. |
| `northwind.py` | The GUI app — customers → orders → line items, plus a report bar. |

## Running it

Copy this folder to the SD card (or the flash disk) and, at the prompt:

```python
run("/sd/northwind/northwind.py")
```

The **first run builds and seeds** `northwind.db` (a second or two); after
that it opens instantly and the data survives a power-off. Use a USB mouse
(or the touch panel); the *Find* box also takes the keyboard.

You can also poke at the data from the REPL without the GUI:

```python
import sys; sys.path.append("/sd/northwind")
import nwdata, nwquery
con = nwdata.open_db("/sd/northwind.db")
for row in nwquery.top_products(con, 5):
    print(row)
```

## The eight tables

`Categories`, `Suppliers`, `Shippers`, `Employees`, `Customers`, `Products`,
`Orders`, `OrderDetails` — the real Northwind shape, trimmed to a few dozen
rows per table so the whole database is tiny and fast on the machine.

## Testing the SQL on a desktop

Because the data and queries use only the DB-API surface that on-device
`usqlite` shares with desktop `sqlite3`, the schema, seed and every query can
be exercised on a PC (`build()` takes any open connection):

```python
import sqlite3, nwdata, nwquery
con = sqlite3.connect(":memory:"); con.isolation_level = None   # autocommit, like usqlite
nwdata.build(con)
print(nwquery.sales_by_category(con))
```
