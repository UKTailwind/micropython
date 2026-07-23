# Appendix J — The Northwind database

Chapter 28 built Northwind's questions (`nwquery.py`) and its screen
(`northwind.py`) in the text. This is the third file — the one that
holds the data and builds the database the first time you open it. It
lives here rather than in the chapter because it is mostly *plumbing*
and *data*: eight `CREATE TABLE`s, a few hundred rows of the sample
Northwind, and the loop that inserts them.

Three things are worth pointing out before the listing:

- **The schema is eight tables, parents before children.** A
  `Products` row *references* a `Suppliers` row and a `Categories`
  row; an `OrderDetails` row references an `Orders` row and a
  `Products` row. Those `REFERENCES` clauses are **foreign keys** —
  they write down the links the queries follow, and let SQLite check
  them.

- **The seed data is plain Python** — lists of tuples — so the file
  imports on a desktop as happily as on the machine, and reads like a
  spreadsheet. `SEED` pairs each table with its column list so one
  loop can fill them all.

- **`build()` wraps every insert in one `BEGIN … COMMIT`.** That is
  the one-line fix promised in the chapter: instead of the database
  saving after each of ~150 rows, it saves once, at the end. (And it
  inserts one `execute()` per row rather than `executemany()`, because
  on this machine `executemany()` runs a multi-statement *script* and
  takes no parameter list — so parameterised bulk loads loop over
  `execute()`, which behaves the same on `usqlite` and desktop
  `sqlite3`.)

`open_db()` at the foot ties it together: open the file, and if the
tables aren't there yet, build and seed them. The chapter's
`northwind.py` calls it once and never thinks about loading again.
`edit("nwdata.py")`:

```python
# Northwind -- schema and seed data for the Pico Computer 3.
#
# A small approximation of Microsoft's classic "Northwind"
# sample database: a fictional food importer's customers,
# products and orders. Row counts are trimmed to a few dozen so
# the whole thing builds in a second, but the eight-table shape
# is the real thing, so the joins in nwquery.py read like
# textbook Northwind queries.
#
# The data is plain Python -- lists of tuples -- so this module
# imports on a PC too; build(con) takes an already-open
# connection, so it runs the same on usqlite and sqlite3.
# Everyday use goes through open_db() at the bottom.

# --- schema ---------------------------------------------------
# One CREATE per table; parents before the children.
SCHEMA = (
    "CREATE TABLE Categories ("
    " CategoryID INTEGER PRIMARY KEY,"
    " CategoryName TEXT NOT NULL,"
    " Description TEXT)",

    "CREATE TABLE Suppliers ("
    " SupplierID INTEGER PRIMARY KEY,"
    " CompanyName TEXT NOT NULL,"
    " City TEXT,"
    " Country TEXT)",

    "CREATE TABLE Shippers ("
    " ShipperID INTEGER PRIMARY KEY,"
    " CompanyName TEXT NOT NULL)",

    "CREATE TABLE Employees ("
    " EmployeeID INTEGER PRIMARY KEY,"
    " LastName TEXT NOT NULL,"
    " FirstName TEXT NOT NULL,"
    " Title TEXT)",

    "CREATE TABLE Customers ("
    " CustomerID TEXT PRIMARY KEY,"
    " CompanyName TEXT NOT NULL,"
    " ContactName TEXT,"
    " City TEXT,"
    " Country TEXT)",

    "CREATE TABLE Products ("
    " ProductID INTEGER PRIMARY KEY,"
    " ProductName TEXT NOT NULL,"
    " SupplierID INTEGER REFERENCES Suppliers(SupplierID),"
    " CategoryID INTEGER REFERENCES Categories(CategoryID),"
    " UnitPrice REAL,"
    " UnitsInStock INTEGER)",

    "CREATE TABLE Orders ("
    " OrderID INTEGER PRIMARY KEY,"
    " CustomerID TEXT REFERENCES Customers(CustomerID),"
    " EmployeeID INTEGER REFERENCES Employees(EmployeeID),"
    " OrderDate TEXT,"
    " ShipVia INTEGER REFERENCES Shippers(ShipperID))",

    "CREATE TABLE OrderDetails ("
    " OrderID INTEGER REFERENCES Orders(OrderID),"
    " ProductID INTEGER REFERENCES Products(ProductID),"
    " UnitPrice REAL,"
    " Quantity INTEGER,"
    " Discount REAL,"
    " PRIMARY KEY (OrderID, ProductID))",
)

# Indexes on the foreign keys the reports group and join by.
INDEXES = (
    "CREATE INDEX ix_products_category ON Products(CategoryID)",
    "CREATE INDEX ix_orders_customer ON Orders(CustomerID)",
    "CREATE INDEX ix_details_order ON OrderDetails(OrderID)",
    "CREATE INDEX ix_details_product ON OrderDetails(ProductID)",
)

# --- seed data ------------------------------------------------
# The real Northwind names, de-accented so they render in the
# machine's bitmap fonts (the database is happy with UTF-8).

CATEGORIES = (
    (1, "Beverages", "Soft drinks, coffees, teas, beers, ales"),
    (2, "Condiments", "Sweet and savoury sauces and relishes"),
    (3, "Confections", "Desserts, candies and sweet breads"),
    (4, "Dairy Products", "Cheeses"),
    (5, "Grains/Cereals", "Breads, crackers, pasta and cereal"),
    (6, "Meat/Poultry", "Prepared meats"),
    (7, "Produce", "Dried fruit and bean curd"),
    (8, "Seafood", "Seaweed and fish"),
)

SUPPLIERS = (
    (1, "Exotic Liquids", "London", "UK"),
    (2, "New Orleans Cajun Delights", "New Orleans", "USA"),
    (3, "Grandma Kelly's Homestead", "Ann Arbor", "USA"),
    (4, "Tokyo Traders", "Tokyo", "Japan"),
    (5, "Cooperativa de Quesos", "Oviedo", "Spain"),
    (6, "Mayumi's", "Osaka", "Japan"),
    (7, "Pavlova Ltd", "Melbourne", "Australia"),
    (8, "Specialty Biscuits", "Manchester", "UK"),
    (9, "PB Knackebrod", "Goteborg", "Sweden"),
    (10, "Refrescos Americanas", "Sao Paulo", "Brazil"),
)

SHIPPERS = (
    (1, "Speedy Express"),
    (2, "United Package"),
    (3, "Federal Shipping"),
)

EMPLOYEES = (
    (1, "Davolio", "Nancy", "Sales Representative"),
    (2, "Fuller", "Andrew", "Vice President, Sales"),
    (3, "Leverling", "Janet", "Sales Representative"),
    (4, "Peacock", "Margaret", "Sales Representative"),
    (5, "Buchanan", "Steven", "Sales Manager"),
)

CUSTOMERS = (
    ("ALFKI", "Alfreds Futterkiste", "Maria Anders",
     "Berlin", "Germany"),
    ("ANATR", "Ana Trujillo Emparedados", "Ana Trujillo",
     "Mexico D.F.", "Mexico"),
    ("AROUT", "Around the Horn", "Thomas Hardy",
     "London", "UK"),
    ("BERGS", "Berglunds snabbkop", "Christina Berglund",
     "Lulea", "Sweden"),
    ("BLAUS", "Blauer See Delikatessen", "Hanna Moos",
     "Mannheim", "Germany"),
    ("BONAP", "Bon app'", "Laurence Lebihan",
     "Marseille", "France"),
    ("BOTTM", "Bottom-Dollar Markets", "Elizabeth Lincoln",
     "Tsawassen", "Canada"),
    ("CACTU", "Cactus Comidas", "Patricio Simpson",
     "Buenos Aires", "Argentina"),
    ("CHOPS", "Chop-suey Chinese", "Yang Wang",
     "Bern", "Switzerland"),
    ("DRACD", "Drachenblut Delikatessen", "Sven Ottlieb",
     "Aachen", "Germany"),
    ("EASTC", "Eastern Connection", "Ann Devon",
     "London", "UK"),
    ("ERNSH", "Ernst Handel", "Roland Mendel",
     "Graz", "Austria"),
    ("FOLKO", "Folk och fa HB", "Maria Larsson",
     "Bracke", "Sweden"),
    ("FRANK", "Frankenversand", "Peter Franken",
     "Munich", "Germany"),
    ("GODOS", "Godos Cocina Tipica", "Jose Pedro Freyre",
     "Seville", "Spain"),
)

PRODUCTS = (
    # ID, name, supplier, category, price, in-stock
    (1, "Chai", 1, 1, 18.00, 39),
    (2, "Chang", 1, 1, 19.00, 17),
    (3, "Aniseed Syrup", 1, 2, 10.00, 13),
    (4, "Chef Anton's Cajun Seasoning", 2, 2, 22.00, 53),
    (5, "Louisiana Gumbo Mix", 2, 2, 21.35, 0),
    (6, "Grandma's Boysenberry Spread", 3, 2, 25.00, 120),
    (7, "Uncle Bob's Dried Pears", 3, 7, 30.00, 15),
    (8, "Northwoods Cranberry Sauce", 3, 2, 40.00, 6),
    (9, "Mishi Kobe Niku", 4, 6, 97.00, 29),
    (10, "Ikura", 4, 8, 31.00, 31),
    (11, "Queso Cabrales", 5, 4, 21.00, 22),
    (12, "Queso Manchego", 5, 4, 38.00, 86),
    (13, "Konbu", 6, 8, 6.00, 24),
    (14, "Tofu", 6, 7, 23.25, 35),
    (15, "Genen Shouyu", 6, 2, 15.50, 39),
    (16, "Pavlova", 7, 3, 17.45, 29),
    (17, "Alice Mutton", 7, 6, 39.00, 0),
    (18, "Carnarvon Tigers", 7, 8, 62.50, 42),
    (19, "Teatime Chocolate Biscuits", 8, 3, 9.20, 25),
    (20, "Sir Rodney's Marmalade", 8, 3, 81.00, 40),
    (21, "Sir Rodney's Scones", 8, 3, 10.00, 3),
    (22, "Gustaf's Knackebrod", 9, 5, 21.00, 104),
    (23, "Tunnbrod", 9, 5, 9.00, 61),
    (24, "Guarana Fantastica", 10, 1, 4.50, 20),
    (25, "Outback Lager", 7, 1, 15.00, 15),
)

# Orders: ID, customer, employee, date (ISO so it sorts and
# date()s cleanly), shipper. IDs start at 10248, as in Northwind.
ORDERS = (
    (10248, "ALFKI", 4, "1997-07-04", 3),
    (10249, "AROUT", 1, "1997-07-05", 1),
    (10250, "BERGS", 3, "1997-07-08", 2),
    (10251, "ERNSH", 5, "1997-07-09", 1),
    (10252, "BONAP", 4, "1997-07-10", 2),
    (10253, "CHOPS", 3, "1997-07-16", 2),
    (10254, "ALFKI", 5, "1997-07-23", 2),
    (10255, "FRANK", 4, "1997-08-01", 3),
    (10256, "BOTTM", 3, "1997-08-12", 2),
    (10257, "ERNSH", 4, "1997-08-22", 3),
    (10258, "GODOS", 1, "1997-09-02", 1),
    (10259, "AROUT", 4, "1997-09-15", 3),
    (10260, "BLAUS", 4, "1997-09-27", 1),
    (10261, "ERNSH", 4, "1997-10-08", 2),
    (10262, "FOLKO", 5, "1997-10-19", 3),
    (10263, "ANATR", 2, "1997-10-31", 1),
    (10264, "BERGS", 3, "1997-11-12", 2),
    (10265, "EASTC", 3, "1997-11-24", 2),
    (10266, "GODOS", 1, "1997-12-06", 3),
    (10267, "FRANK", 4, "1997-12-18", 1),
    (10268, "CACTU", 1, "1998-01-05", 3),
    (10269, "DRACD", 5, "1998-01-19", 1),
    (10270, "ERNSH", 1, "1998-02-02", 1),
    (10271, "ALFKI", 4, "1998-02-16", 2),
)

# Order lines: order, product, unit price, quantity, discount.
# Prices mostly match the product's list price; a couple differ.
ORDERDETAILS = (
    (10248, 1, 18.00, 12, 0.0),
    (10248, 22, 21.00, 10, 0.0),
    (10248, 19, 9.20, 5, 0.0),
    (10249, 14, 23.25, 9, 0.0),
    (10249, 18, 62.50, 40, 0.0),
    (10250, 10, 31.00, 35, 0.15),
    (10250, 12, 38.00, 15, 0.15),
    (10250, 2, 19.00, 10, 0.0),
    (10251, 6, 25.00, 6, 0.05),
    (10251, 20, 81.00, 15, 0.05),
    (10251, 4, 22.00, 20, 0.0),
    (10252, 16, 17.45, 40, 0.05),
    (10252, 9, 97.00, 25, 0.05),
    (10253, 3, 10.00, 20, 0.0),
    (10253, 11, 21.00, 42, 0.0),
    (10253, 13, 6.00, 40, 0.0),
    (10254, 24, 4.50, 15, 0.15),
    (10254, 25, 15.00, 21, 0.15),
    (10255, 2, 19.00, 20, 0.0),
    (10255, 16, 17.45, 35, 0.0),
    (10255, 7, 30.00, 30, 0.0),
    (10256, 12, 38.00, 15, 0.0),
    (10256, 6, 25.00, 12, 0.0),
    (10257, 9, 97.00, 25, 0.0),
    (10257, 18, 62.50, 6, 0.0),
    (10257, 12, 38.00, 15, 0.0),
    (10258, 1, 18.00, 50, 0.2),
    (10258, 11, 21.00, 65, 0.2),
    (10258, 20, 81.00, 6, 0.2),
    (10259, 21, 10.00, 10, 0.0),
    (10259, 4, 22.00, 1, 0.0),
    (10260, 14, 23.25, 21, 0.25),
    (10260, 20, 81.00, 15, 0.0),
    (10260, 23, 9.00, 44, 0.25),
    (10261, 22, 21.00, 20, 0.0),
    (10261, 8, 40.00, 20, 0.0),
    (10262, 5, 21.35, 12, 0.2),
    (10262, 7, 30.00, 15, 0.0),
    (10263, 16, 17.45, 60, 0.25),
    (10263, 24, 4.50, 28, 0.0),
    (10263, 12, 38.00, 40, 0.25),
    (10264, 2, 19.00, 35, 0.0),
    (10264, 20, 81.00, 25, 0.15),
    (10265, 17, 39.00, 30, 0.0),
    (10265, 10, 31.00, 20, 0.0),
    (10266, 12, 38.00, 12, 0.05),
    (10266, 18, 62.50, 10, 0.05),
    (10267, 25, 15.00, 48, 0.15),
    (10267, 1, 18.00, 20, 0.0),
    (10267, 6, 25.00, 70, 0.15),
    (10268, 22, 21.00, 4, 0.0),
    (10268, 13, 6.00, 10, 0.0),
    (10269, 9, 97.00, 60, 0.05),
    (10269, 18, 62.50, 20, 0.05),
    (10270, 12, 38.00, 30, 0.0),
    (10270, 4, 22.00, 25, 0.0),
    (10270, 8, 40.00, 12, 0.0),
    (10271, 1, 18.00, 24, 0.0),
    (10271, 16, 17.45, 12, 0.0),
    (10271, 19, 9.20, 30, 0.0),
)

# Each table with its column list, for a tidy loop in build().
SEED = (
    ("Categories",
     "CategoryID, CategoryName, Description", CATEGORIES),
    ("Suppliers",
     "SupplierID, CompanyName, City, Country", SUPPLIERS),
    ("Shippers", "ShipperID, CompanyName", SHIPPERS),
    ("Employees",
     "EmployeeID, LastName, FirstName, Title", EMPLOYEES),
    ("Customers",
     "CustomerID, CompanyName, ContactName, City, Country",
     CUSTOMERS),
    ("Products",
     "ProductID, ProductName, SupplierID, CategoryID,"
     " UnitPrice, UnitsInStock", PRODUCTS),
    ("Orders",
     "OrderID, CustomerID, EmployeeID, OrderDate, ShipVia",
     ORDERS),
    ("OrderDetails",
     "OrderID, ProductID, UnitPrice, Quantity, Discount",
     ORDERDETAILS),
)


# --- building -------------------------------------------------
def _placeholders(cols):
    return ", ".join("?" for _ in cols.split(","))


def build(con):
    """Create every table + index and load the seed data on the
    open connection `con`, all inside one transaction -- a build
    either lands whole or not at all, and a single COMMIT is far
    faster than committing every insert on the flash disk.

    We insert one execute()-per-row, not executemany(): usqlite's
    executemany() runs a multi-statement SQL script and takes no
    parameter list, so parameterised bulk loads use execute() in
    a loop -- which behaves the same on usqlite and sqlite3."""
    con.execute("BEGIN")
    for stmt in SCHEMA:
        con.execute(stmt)
    for table, cols, rows in SEED:
        sql = ("INSERT INTO %s (%s) VALUES (%s)"
               % (table, cols, _placeholders(cols)))
        for row in rows:
            con.execute(sql, row)
    for stmt in INDEXES:
        con.execute(stmt)
    con.execute("COMMIT")


def _built(con):
    """True if the database already has our tables."""
    cur = con.execute(
        "SELECT count(*) FROM sqlite_master"
        " WHERE type='table' AND name='Orders'")
    return cur.fetchone()[0] == 1


def open_db(path="/sd/northwind.db", force=False):
    """Open (building it the first time) the Northwind database
    and return the live connection. The first call on a fresh
    machine creates and seeds the file, which then survives a
    power-cycle; later calls just open it. force=True rebuilds."""
    import usqlite  # lazy so this file also imports on a PC

    if force:
        try:
            import os
            os.remove(path)
        except OSError:
            pass
    con = usqlite.connect(path)
    if not _built(con):
        build(con)
    return con
```

To poke at this data yourself without the GUI, put the folder on the
import path and open it at the `>>>` prompt:

```python
>>> import sys
>>> sys.path.append("/sd/northwind")
>>> import nwdata
>>> con = nwdata.open_db("/sd/northwind.db")
>>> con.execute("SELECT count(*) FROM Orders").fetchone()
(24,)
>>> con.execute("SELECT ProductName FROM Products"
...             " WHERE UnitsInStock = 0").fetchall()
[('Louisiana Gumbo Mix',), ('Alice Mutton',)]
```
