# Chapter 28 — Databases: keeping and querying real data

Chapter 12 taught your programs to remember by writing files, and
you have leaned on that ever since — high scores, saved worlds,
`prefs.txt`. Files are perfect for a handful of lines. But the moment
data grows *rows* and *questions* — "which customer spent the most?",
"show me every order in July, newest first" — reading the whole file
and sifting it by hand in Python turns into a chore, and a slow one.

There is a better tool, and this machine has it built in: a
**database**. Specifically **SQLite**, the same engine inside your
phone, your browser and aeroplanes — here as the `usqlite` module.
You keep data in **tables**, and you ask for exactly what you want in
**SQL**, a small language for questions. The database does the
searching, sorting and sums; you get back just the answer. By the end
of the chapter you will have built a point-and-click business
database — Part V's GUI from chapter 27, now with a real engine
behind it.

## A table in ten seconds

A database lives in a single file. Open one and it is yours; open it
again next week and everything is still there. At the `>>>` prompt:

```python
>>> import usqlite
>>> db = usqlite.connect("/sd/first.db")
>>> db.execute("CREATE TABLE pets(name TEXT, legs INTEGER)")
>>> db.execute("INSERT INTO pets VALUES ('Rex', 4)")
>>> db.execute("INSERT INTO pets VALUES ('Kea', 2)")
>>> for row in db.execute("SELECT * FROM pets"):
...     print(row)
...
('Rex', 4)
('Kea', 2)
```

Six lines and you have a working database on the SD card. Unpick
them, because every one is a whole idea:

- **`connect`** opens (or, the first time, *creates*) the file and
  hands back a connection — your handle to the database, like `open`
  in chapter 12 but for a whole file of tables.
- **`CREATE TABLE`** declares a table's shape once: named **columns**,
  each with a type (`TEXT`, `INTEGER`, `REAL`). A table is a grid —
  columns across, one **row** per thing.
- **`INSERT`** adds a row. **`SELECT`** asks for rows back;
  `SELECT *` means "every column". The words in capitals are SQL, not
  Python — you are speaking to the database *through* Python.
- **`execute` hands back a cursor** — a pointer at the results. For
  `CREATE` and `INSERT` there are none, so we ignore it; for `SELECT`
  you loop over it and each row arrives as a tuple.

And the quiet miracle: you never wrote a save. **Every statement is
committed the instant it runs** — pull the power after that last
`INSERT` and Kea is still there tomorrow. (When you have *many* rows
to add at once, that per-row saving is slow; the project below shows
the one-line fix.)

> **Coming from MMBasic:** this is genuinely new ground. The Micromite
> stored data in arrays and flat files, or `VAR SAVE` for a few
> globals — there was no query language. SQL is a different way of
> thinking: you describe *what* you want, not *how* to loop for it,
> and the engine works out the rest.

## The golden rule: parameters, never patchwork

You will often insert values that live in variables. The temptation
is to build the SQL string yourself:

```python
>>> name = "Rex"
>>> # DON'T: gluing the value into the SQL by hand
>>> db.execute("INSERT INTO pets VALUES ('" + name + "', 4)")
```

It works — until a name contains an apostrophe. `"O'Hara"` closes the
quote early and the statement becomes gibberish; a *mischievous* name
could rewrite your query entirely (this is the famous "SQL injection"
bug). The cure is a **placeholder**: write `?` in the SQL and pass the
values separately, and the database inserts them safely, quotes and
all:

```python
>>> db.execute("INSERT INTO pets VALUES (?, ?)", ("O'Hara", 4))
>>> db.execute("SELECT * FROM pets WHERE name = ?", ("O'Hara",))
```

The values go in a tuple, one per `?`. Make this a reflex: **every
value from a variable, a file or a human goes in through `?`** — never
glued into the string. It is shorter, it is safe, and from here on
every query in the chapter uses it.

## A real program: the high-score table

Remember chapter 12's `scorelib.py`, keeping your games' high scores
in a text file — reading it all in, splitting each line, sorting by
hand? Here is the same job as a database, and watch how much *work*
moves out of your code and into SQL. `edit("scores.py")`:

```python
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
```

Run it a few times with different names and scores; the board fills
up and stays filled between runs. The lessons:

- **`CREATE TABLE IF NOT EXISTS`** is the honest way to open a
  database that may or may not exist yet: make the table the first
  time, do nothing every time after. No more chapter 13 `try` around
  a missing file.
- **The leaderboard is one line of SQL.** `ORDER BY score DESC` sorts
  highest-first; `LIMIT ?` keeps the top `n`. Chapter 12 did this with
  `sorted(..., reverse=True)[:10]` *and* the parsing that fed it —
  here the engine does the lot, over thousands of rows, in an instant.
- **`.fetchall()`** drains the cursor into a plain list of tuples, so
  `top()` can hand the results back and close over nothing — an
  ordinary function returning ordinary data.

## Asking sharper questions

`SELECT` is where a database earns its keep. Everything narrows or
reshapes the rows *before* they reach you. A quick tour, against the
`pets` table:

```python
>>> # only some rows
>>> db.execute("SELECT name FROM pets WHERE legs = 4").fetchall()
[('Rex',), ("O'Hara",)]
>>> # a fuzzy match: names containing 'e'
>>> db.execute("SELECT name FROM pets"
...            " WHERE name LIKE '%e%'").fetchall()
[('Rex',), ('Kea',)]
```

`WHERE` filters; `LIKE '%e%'` matches any text with an `e` in it (`%`
is "anything here"). And the database can *compute* over the whole
table without handing you a single row — the **aggregate** functions:

```python
>>> db.execute("SELECT count(*) FROM pets").fetchone()
(3,)
>>> db.execute("SELECT sum(legs), avg(legs) FROM pets").fetchone()
(10, 3.333333)
```

`count`, `sum`, `avg`, `min`, `max` — the questions a spreadsheet
answers, asked in SQL. `.fetchone()` takes just the first row, which
is all an aggregate produces. Pair them with **`GROUP BY`** and you
get one answer *per group* — "how many pets have each leg-count?":

```python
>>> db.execute("SELECT legs, count(*) FROM pets"
...            " GROUP BY legs").fetchall()
[(2, 1), (4, 2)]
```

Two-legged: one. Four-legged: two. That single line replaces a
dictionary, a loop and a running tally — chapter 10's counting
pattern, handed to the engine.

## Relationships: joining tables

Real data comes in *related* pieces. An order belongs to a customer;
an order line names a product. You *could* cram it all into one wide
table, repeating the customer's name and city on every order — but
then a change of address means editing a hundred rows, and a typo in
one is a lie. The database way is to keep each fact **once**, in its
own table, and link them by an **id**:

![The Traders database — five tables, each holding one kind of fact, linked by id columns. A `Line` points to its `Order` (which `OrderID`) and to a `Product` (which `ProdID`); an `Order` points to its `Customer`; a `Product` points to its `Category`. Follow the arrows and any question — "what did Berglunds buy in July?" — becomes a path through the tables. That path is a JOIN.](figs/28-schema.png)

To answer a real question you walk the links, and SQL's word for
walking a link is **`JOIN`**: "match each row here to its row there".
Which order lines are on order 2, and what are they called?

```python
>>> db.execute(
...   "SELECT p.Name, l.Qty FROM Lines l"
...   " JOIN Products p ON p.ProdID = l.ProdID"
...   " WHERE l.OrderID = 2").fetchall()
[('Queso Cabrales', 9), ('Queso Manchego', 40)]
```

Read the `JOIN` as glue: *for each row of `Lines`, find the `Products`
row whose `ProdID` matches, and let me use its columns too.* The
short names `l` and `p` (table **aliases**) keep it readable. Join
three tables and you can go from a customer straight to the products
they bought — which is exactly what the project does next.

## The project: Traders

Time to put it together: a small business database with a
point-and-click front end. **Traders** keeps customers, the products
they order, and the lines on each order — and lets you browse them
with the GUI toolkit from chapter 27. Click a customer to see their
orders; click an order to see what was on it and what it cost; press
a button for a best-sellers report. `edit("traders.py")`:

```python
import time
import pcgui
import usqlite
from pcgfx import WHITE, YELLOW, CYAN, GOLD, RED, COBALT

DB = "/sd/traders.db"

CATEGORIES = (
    (1, "Beverages"), (2, "Cheeses"), (3, "Confections"),
)
PRODUCTS = (
    (1, "Chai", 1, 18.00), (2, "Chang", 1, 19.00),
    (3, "Queso Cabrales", 2, 21.00),
    (4, "Queso Manchego", 2, 38.00),
    (5, "Pavlova", 3, 17.45), (6, "Scones", 3, 10.00),
)
CUSTOMERS = (
    ("ALFKI", "Alfreds Futterkiste", "Berlin"),
    ("AROUT", "Around the Horn", "London"),
    ("BERGS", "Berglunds", "Lulea"),
    ("ERNSH", "Ernst Handel", "Graz"),
)
ORDERS = (
    (1, "ALFKI", "1997-07-04"), (2, "AROUT", "1997-07-05"),
    (3, "BERGS", "1997-07-08"), (4, "ERNSH", "1997-07-09"),
    (5, "ALFKI", "1997-07-23"), (6, "ERNSH", "1997-08-22"),
)
LINES = (
    (1, 1, 12), (1, 6, 5), (2, 3, 9), (2, 4, 40),
    (3, 5, 15), (3, 2, 10), (4, 3, 20), (4, 5, 6),
    (5, 1, 15), (5, 6, 21), (6, 4, 25), (6, 2, 30),
)


def build(con):
    con.execute("BEGIN")
    con.execute("CREATE TABLE Categories("
                "CatID INTEGER PRIMARY KEY, Name TEXT)")
    con.execute("CREATE TABLE Products(ProdID INTEGER"
                " PRIMARY KEY, Name TEXT, CatID INTEGER,"
                " Price REAL)")
    con.execute("CREATE TABLE Customers("
                "CustID TEXT PRIMARY KEY, Name TEXT, City TEXT)")
    con.execute("CREATE TABLE Orders(OrderID INTEGER PRIMARY KEY,"
                "CustID TEXT, OrderDate TEXT)")
    con.execute("CREATE TABLE Lines("
                "OrderID INTEGER, ProdID INTEGER, Qty INTEGER)")
    for row in CATEGORIES:
        con.execute("INSERT INTO Categories VALUES (?,?)", row)
    for row in PRODUCTS:
        con.execute("INSERT INTO Products VALUES (?,?,?,?)", row)
    for row in CUSTOMERS:
        con.execute("INSERT INTO Customers VALUES (?,?,?)", row)
    for row in ORDERS:
        con.execute("INSERT INTO Orders VALUES (?,?,?)", row)
    for row in LINES:
        con.execute("INSERT INTO Lines VALUES (?,?,?)", row)
    con.execute("COMMIT")


def open_db():
    con = usqlite.connect(DB)
    have = con.execute("SELECT count(*) FROM sqlite_master"
                       " WHERE name='Orders'").fetchone()[0]
    if not have:
        build(con)
    return con


def orders_of(con, cust):
    return con.execute(
        "SELECT o.OrderID, o.OrderDate,"
        " sum(p.Price * l.Qty) AS Total"
        " FROM Orders o"
        " JOIN Lines l ON l.OrderID = o.OrderID"
        " JOIN Products p ON p.ProdID = l.ProdID"
        " WHERE o.CustID = ?"
        " GROUP BY o.OrderID"
        " ORDER BY o.OrderDate", (cust,)).fetchall()


def lines_of(con, order):
    return con.execute(
        "SELECT p.Name, l.Qty, p.Price, p.Price * l.Qty AS Amount"
        " FROM Lines l"
        " JOIN Products p ON p.ProdID = l.ProdID"
        " WHERE l.OrderID = ?"
        " ORDER BY p.Name", (order,)).fetchall()


def top_products(con):
    return con.execute(
        "SELECT p.Name, sum(l.Qty) AS Units,"
        " sum(p.Price * l.Qty) AS Revenue"
        " FROM Lines l"
        " JOIN Products p ON p.ProdID = l.ProdID"
        " GROUP BY p.ProdID"
        " ORDER BY Revenue DESC", ()).fetchall()


def customers(con):
    return con.execute(
        "SELECT CustID, Name, City FROM Customers"
        " ORDER BY Name", ()).fetchall()


def fit(s, n):
    s = str(s)
    return s[:n] if len(s) >= n else s + " " * (n - len(s))


def main():
    con = open_db()
    screen(hdmi.RGB640)
    time.sleep(3)
    console("serial")
    hdmi.fb().fill(0)

    g = pcgui.GUI()
    g.start()
    done = [False]
    custs = customers(con)
    orders = [[]]                   # rows behind the orders list

    g.caption(8, 8, "TRADERS", fg=YELLOW, font=2)
    g.button(560, 6, 72, 24, "EXIT", fg=WHITE, bg=RED,
             callback=lambda b: done.__setitem__(0, True))
    g.displaybox(8, 40, 250, 20, "Customers", fg=CYAN)
    g.displaybox(266, 40, 366, 20, "Orders", fg=CYAN)
    hdr = g.displaybox(8, 300, 476, 20, "Order lines", fg=CYAN)
    tot = g.displaybox(492, 300, 140, 20, "Total: 0.00", fg=GOLD)
    lb_ord = g.listbox(266, 64, 366, 224, [], font=1)
    lb_det = g.listbox(8, 324, 624, 130, [], font=1)

    def fill(lb, items):
        lb.items = items
        lb.top = 0
        lb._value = 0
        lb.draw()

    def show_lines(idx):
        rows = lines_of(con, orders[0][idx][0])
        items = ["%s %3d x %7.2f = %9.2f"
                 % (fit(r[0], 20), r[1], r[2], r[3])
                 for r in rows]
        fill(lb_det, items)
        hdr.value = "Order #%d" % orders[0][idx][0]
        tot.value = "Total: %.2f" % sum(r[3] for r in rows)

    def show_orders(idx):
        orders[0] = orders_of(con, custs[idx][0])
        fill(lb_ord, ["#%d  %s  %9.2f" % (r[0], r[1], r[2])
                      for r in orders[0]])
        if orders[0]:
            show_lines(0)

    def on_cust(lb):
        show_orders(lb.value)

    def on_order(lb):
        if orders[0]:
            show_lines(lb.value)

    def report(b):
        rows = top_products(con)
        items = ["%s  units %3d   revenue %9.2f"
                 % (fit(r[0], 20), r[1], r[2])
                 for r in rows]
        fill(lb_det, items)
        hdr.value = "Top products by revenue"
        tot.value = ""

    lb_cust = g.listbox(8, 64, 250, 224,
                        ["%s (%s)" % (c[1], c[2]) for c in custs],
                        font=1, callback=on_cust)
    lb_ord.callback = on_order
    g.button(8, 460, 150, 26, "Top Products", bg=COBALT,
             callback=report)

    show_orders(0)
    try:
        while not done[0]:
            g.poll()
            time.sleep_ms(10)
    finally:
        g.stop()
        console()
        con.close()


if __name__ == "__main__":
    main()
```

It is a longer program, but you have met every part of it before —
so read it in three layers:

- **The data and `build()`** are chapter 12's "write it if it's
  missing", grown up. `open_db()` peeks in `sqlite_master` (SQLite's
  private catalogue of tables) to see whether we have built before; if
  not, `build()` creates the five tables and fills them. Note the
  **`BEGIN` … `COMMIT`** wrapping every insert: instead of saving
  sixty times, the database saves *once*, at the end — the fast way to
  load a batch, and the fix promised earlier. (And why a loop of
  `execute`, not one `executemany`? On this machine `executemany`
  runs a multi-statement *script*, not one statement over many rows —
  so parameterised bulk loads use `execute` in a `for`.)
- **The four query functions are the heart** — and they are pure SQL,
  each returning a list of tuples. `orders_of` joins three tables and
  sums each order with `GROUP BY`; `top_products` groups by product
  across *every* order; `lines_of` joins a line to its product for the
  name. This is the chapter's whole argument in four functions: the
  hard work is a sentence of SQL, not a page of loops.
- **`main()` is chapter 27, unchanged in spirit** — a `pcgui`
  manager, a poll loop, controls with callbacks. The only new idea is
  wiring a **list to a query**: picking a customer runs `orders_of`
  and re-fills the orders list (`fill` swaps a listbox's items and
  redraws); picking an order runs `lines_of`. The GUI is a *window
  onto the database* — it stores nothing itself, exactly chapter 27's
  "controls are a view of the data, never its home".

## The finished article: Northwind

Traders is a scale model of a classic: **Northwind**, Microsoft's
long-serving sample database of a food importer. The machine ships
with the full version — eight tables, dozens of products and orders,
a search box, and four business reports (best sellers, sales by
category, sales per employee, low stock) — in the `northwind/`
folder. It is the very same shapes you just built, only larger: the
schema in `nwdata.py`, the queries in `nwquery.py` (worth reading —
they are Traders' four functions with a few more joins), and the
`pcgui` browser in `northwind.py`. Copy the folder to `/sd` and:

```python
>>> run("/sd/northwind/northwind.py")
```

Everything in this chapter is standing behind that one screen. When
you click a customer and their orders and totals appear instantly,
that is a three-table `JOIN` and a `GROUP BY` running on the same
chip that draws the window — a real database and a real user
interface, meeting in about four hundred lines of Python. Not bad for
a machine that fits in your hand.

## Experiments

1. Give `scores.py` a per-game board: add a `game TEXT` column, ask
   for the game name too, and change `top()` to take a game and add
   `WHERE game = ?`. One column, one clause — a feature that would
   have been a rewrite with files.
2. Add a **Categories** view to Traders: a second report button whose
   query is `sum(Price*Qty)` grouped by `Categories.Name` (join
   `Lines → Products → Categories`). Three tables, one answer — the
   pattern every business report is made of.
3. Delete and update: give Traders a query that runs
   `"DELETE FROM Lines WHERE OrderID = ?"` and one with
   `"UPDATE Products SET Price = ? WHERE ProdID = ?"`. Watch the
   totals change on the next click — the database is *live*, not a
   snapshot.
4. Ask the full Northwind a question of your own at the `>>>` prompt:
   `import nwdata`, `con = nwdata.open_db("/sd/northwind.db")`, then
   write a `SELECT` — the busiest month, the customer in the most
   countries, whatever you're curious about. This is what a database
   is *for*.

## Challenges

1. **The library.** Model a book collection: `Books`, `Authors`, and
   a `Loans` table recording who has what out. A GUI that lists books,
   shows an author's whole shelf on a click, and a button for
   "everything currently on loan" (a `JOIN` where the return date is
   `NULL`). You have just re-invented what every library runs.
2. **A search box.** Give Northwind's product list a `textbox`
   (chapter 27's pop-up keyboard) that filters as you type, feeding
   what you type into `WHERE ProductName LIKE ?` with `%` around it.
   The real `northwind.py` does exactly this — build it yourself
   first, then compare.
3. **The logger.** A background program (chapter 34) that every
   minute writes one row — a sensor reading, the free memory, the
   time — into a `readings` table, forever. Then a second program
   that plots the last hour with the maths lab (chapter 31). Days of
   data in a file that never needs emptying, and SQL to slice it any
   way you ask.
4. **Export.** Write a function that turns any query's results into a
   `.csv` file (chapter 12's writing, one row per line, commas
   between) so a day's orders can leave the machine for a spreadsheet.
   A database that can't share is an island; give yours a bridge.
