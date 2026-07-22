# Northwind Traders -- a database browser for the Pico Computer 3.
#
# Puts the two headline features of the machine together: the on-device SQLite
# engine (usqlite) holding a small Northwind-style business database, and the
# pcgui toolkit drawing a real point-and-click front end over it.
#
#   run("/sd/northwind/northwind.py")
#
# Pick a customer on the left to see their orders; pick an order to see its
# line items and total. The buttons along the bottom run whole-database
# reports. The very first run builds and seeds the database file (a second or
# two); after that it opens instantly and the data persists across power-offs.
#
# Needs a USB mouse (or a touch panel). The Find box also takes the keyboard.

import sys
import time

# Make the sibling modules (nwdata, nwquery) importable however this file is
# launched. run() chdirs into the program's folder so a plain `import nwdata`
# usually works, but opening it from the editor or `import northwind` does not
# -- so add our own folder (from __file__) to the search path to be sure.
try:
    _here = __file__.rsplit("/", 1)[0]
    if _here and _here not in sys.path:
        sys.path.insert(0, _here)
except NameError:
    pass

import pcgui
from pcgfx import WHITE, YELLOW, GREEN, CYAN, RED, ORANGE, GOLD, COBALT, LITEGRAY

import nwdata
import nwquery


# --- text helpers (plain %-formatting: fully supported on MicroPython) -------
def fit(s, n):
    """Left-justify s into exactly n characters (truncating if need be)."""
    s = str(s)
    if len(s) >= n:
        return s[:n]
    return s + " " * (n - len(s))


def money(x):
    return "%.2f" % (x or 0.0)


def pick_path():
    """Prefer the SD card; fall back to the flash disk if there's no card."""
    import os

    try:
        os.stat("/sd")
        return "/sd/northwind.db"
    except OSError:
        return "/northwind.db"


class Northwind:
    def __init__(self, g, con):
        self.g = g
        self.con = con
        self.done = False
        self.cust_rows = []     # rows behind lbCust, parallel to its items
        self.order_rows = []    # rows behind lbOrders

    # --- build the screen ---------------------------------------------------
    def build_ui(self):
        g = self.g
        g.caption(8, 8, "NORTHWIND TRADERS", fg=YELLOW, font=2)
        g.caption(360, 12, "on-device SQLite + GUI", fg=LITEGRAY, font=1)
        g.button(556, 6, 76, 24, "EXIT", fg=WHITE, bg=RED, callback=self.on_exit)

        # search row
        g.caption(8, 40, "Find", fg=WHITE, font=1)
        self.tbFind = g.textbox(48, 34, 170, 22, text="", callback=self.on_find)
        g.button(224, 34, 60, 22, "Clear", callback=self.on_clear)

        # dynamic section headers (displayboxes repaint cleanly on change)
        self.dbCustHdr = g.displaybox(8, 64, 250, 20, "Customers", fg=CYAN)
        self.dbOrdHdr = g.displaybox(266, 64, 366, 20, "Orders", fg=CYAN)

        # the two master lists
        self.lbCust = g.listbox(8, 88, 250, 216, [], font=1, callback=self.on_cust)
        self.lbOrders = g.listbox(266, 88, 366, 216, [], font=1, callback=self.on_order)

        # detail header + running total, then the detail/report list
        self.dbDetHdr = g.displaybox(8, 314, 476, 20, "Order lines", fg=CYAN)
        self.dbTotal = g.displaybox(492, 314, 140, 20, "Total: 0.00", fg=GOLD)
        self.lbDetail = g.listbox(8, 338, 624, 104, [], font=1)

        # report bar
        g.button(8, 448, 150, 26, "Top Products", bg=COBALT, callback=self.rep_top)
        g.button(166, 448, 150, 26, "By Category", bg=COBALT, callback=self.rep_cat)
        g.button(324, 448, 150, 26, "By Employee", bg=COBALT, callback=self.rep_emp)
        g.button(482, 448, 150, 26, "Low Stock", bg=COBALT, callback=self.rep_stock)

    # --- filling the lists --------------------------------------------------
    def _set_list(self, lb, items):
        lb.items = items
        lb.top = 0
        lb._value = 0
        lb.draw()

    def load_customers(self, like=""):
        self.cust_rows = nwquery.customers(self.con, like)
        items = ["%s  (%s)" % (r[1], r[2]) for r in self.cust_rows]  # company (city)
        self._set_list(self.lbCust, items)
        self.dbCustHdr.value = "Customers (%d)" % len(self.cust_rows)
        # cascade: show the first customer's orders, or clear if none matched
        if self.cust_rows:
            self.show_orders(0)
        else:
            self._set_list(self.lbOrders, [])
            self.order_rows = []
            self.dbOrdHdr.value = "Orders"
            self.clear_detail()

    def show_orders(self, idx):
        cust = self.cust_rows[idx]                     # (id, company, city, country)
        self.order_rows = nwquery.orders_for_customer(self.con, cust[0])
        # row: (OrderID, OrderDate, Employee, Shipper, Total)
        items = ["#%d  %s  %s  %s" % (r[0], r[1], fit(money(r[4]), 9), r[2])
                 for r in self.order_rows]
        self._set_list(self.lbOrders, items)
        self.dbOrdHdr.value = "Orders for %s (%d)" % (cust[1], len(self.order_rows))
        if self.order_rows:
            self.show_lines(0)
        else:
            self.clear_detail()

    def show_lines(self, idx):
        order = self.order_rows[idx]                   # (id, date, emp, shipper, total)
        lines = nwquery.order_lines(self.con, order[0])
        # row: (ProductName, UnitPrice, Quantity, Discount, LineTotal)
        items = ["%s %3d x %8s  -%2.0f%%  = %9s"
                 % (fit(r[0], 28), r[2], money(r[1]), r[3] * 100, money(r[4]))
                 for r in lines]
        self._set_list(self.lbDetail, items)
        self.dbDetHdr.value = "Order #%d  -  %d line(s)" % (order[0], len(lines))
        self.dbTotal.value = "Total: %s" % money(order[4])

    def clear_detail(self):
        self._set_list(self.lbDetail, [])
        self.dbDetHdr.value = "Order lines"
        self.dbTotal.value = "Total: 0.00"

    def _report(self, title, items):
        self.dbDetHdr.value = title
        self.dbTotal.value = ""
        self._set_list(self.lbDetail, items)

    # --- control callbacks --------------------------------------------------
    def on_cust(self, lb):
        if self.cust_rows:
            self.show_orders(lb.value)

    def on_order(self, lb):
        if self.order_rows:
            self.show_lines(lb.value)

    def on_find(self, tb):
        self.load_customers(tb.value.strip())

    def on_clear(self, b):
        self.tbFind.value = ""
        self.load_customers("")

    def rep_top(self, b):
        rows = nwquery.top_products(self.con, 12)
        items = ["%s  units %4d    revenue %10s"
                 % (fit(r[0], 30), r[1], money(r[2])) for r in rows]
        self._report("Top products by revenue", items)

    def rep_cat(self, b):
        rows = nwquery.sales_by_category(self.con)
        items = ["%s    revenue %10s" % (fit(r[0], 20), money(r[1])) for r in rows]
        self._report("Sales by category", items)

    def rep_emp(self, b):
        rows = nwquery.sales_by_employee(self.con)
        items = ["%s  orders %2d    revenue %10s"
                 % (fit(r[0], 22), r[1], money(r[2])) for r in rows]
        self._report("Sales by employee", items)

    def rep_stock(self, b):
        rows = nwquery.low_stock(self.con, 15)
        items = ["%s  stock %3d    reorder from %s"
                 % (fit(r[0], 28), r[1], r[2]) for r in rows]
        self._report("Low stock (15 or fewer)", items)

    def on_exit(self, b):
        self.done = True


def main():
    print("Opening Northwind database (first run builds it)...")
    con = nwdata.open_db(pick_path())
    print("SQLite", con.execute("SELECT sqlite_version()").fetchone()[0], "ready")

    screen(hdmi.RGB640)
    time.sleep(3)              # let the monitor re-lock after the mode change
    console("serial")
    hdmi.fb().fill(0)

    g = pcgui.GUI()
    g.start()
    app = Northwind(g, con)
    app.build_ui()
    app.load_customers()

    try:
        while not app.done:
            g.poll()
            time.sleep_ms(10)
    finally:
        g.stop()
        console()
        con.close()
        print("Northwind closed")


if __name__ == "__main__":
    main()
