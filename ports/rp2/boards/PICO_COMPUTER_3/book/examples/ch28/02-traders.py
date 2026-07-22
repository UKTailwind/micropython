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
