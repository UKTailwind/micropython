# Northwind-lite -- the queries.
#
# Every function takes an open connection (from nwdata.open_db) and returns a
# list of plain tuples, so the same code runs against on-device `usqlite` and
# desktop `sqlite3`. The column order each one returns is written above it --
# the GUI in northwind.py relies on those orders. This is where the database
# earns its keep: multi-table joins and GROUP BY aggregates that would be a
# tangle of nested loops and dictionaries done by hand.
#
# All user-supplied values go in through `?` parameters, never string-built
# into the SQL -- that is what keeps a name like  O'Brien  (or a malicious one)
# from breaking, or rewriting, the query.


def customers(con, like=""):
    """CustomerID, CompanyName, City, Country -- optionally filtered by a
    name/city fragment. Alphabetical by company."""
    if like:
        pat = "%" + like + "%"
        cur = con.execute(
            "SELECT CustomerID, CompanyName, City, Country FROM Customers"
            " WHERE CompanyName LIKE ? OR City LIKE ?"
            " ORDER BY CompanyName", (pat, pat))
    else:
        cur = con.execute(
            "SELECT CustomerID, CompanyName, City, Country FROM Customers"
            " ORDER BY CompanyName")
    return cur.fetchall()


def products(con, like=""):
    """ProductID, ProductName, CategoryName, UnitPrice, UnitsInStock -- joins
    Products to Categories so each row carries its category's name."""
    if like:
        pat = "%" + like + "%"
        cur = con.execute(
            "SELECT p.ProductID, p.ProductName, c.CategoryName,"
            "       p.UnitPrice, p.UnitsInStock"
            " FROM Products p JOIN Categories c ON c.CategoryID = p.CategoryID"
            " WHERE p.ProductName LIKE ?"
            " ORDER BY p.ProductName", (pat,))
    else:
        cur = con.execute(
            "SELECT p.ProductID, p.ProductName, c.CategoryName,"
            "       p.UnitPrice, p.UnitsInStock"
            " FROM Products p JOIN Categories c ON c.CategoryID = p.CategoryID"
            " ORDER BY p.ProductName")
    return cur.fetchall()


def orders_for_customer(con, customer_id):
    """OrderID, OrderDate, EmployeeName, Shipper, OrderTotal -- one row per
    order for the customer. The total is summed from the order's lines with a
    correlated sub-query, so price x quantity less discount is done in SQL."""
    return con.execute(
        "SELECT o.OrderID, o.OrderDate,"
        "       e.FirstName || ' ' || e.LastName AS Employee,"
        "       s.CompanyName AS Shipper,"
        "       (SELECT sum(d.UnitPrice * d.Quantity * (1 - d.Discount))"
        "          FROM OrderDetails d WHERE d.OrderID = o.OrderID) AS Total"
        " FROM Orders o"
        " JOIN Employees e ON e.EmployeeID = o.EmployeeID"
        " JOIN Shippers  s ON s.ShipperID  = o.ShipVia"
        " WHERE o.CustomerID = ?"
        " ORDER BY o.OrderDate", (customer_id,)).fetchall()


def order_lines(con, order_id):
    """ProductName, UnitPrice, Quantity, Discount, LineTotal -- the items on
    one order, joining each line to its product for the name."""
    return con.execute(
        "SELECT p.ProductName, d.UnitPrice, d.Quantity, d.Discount,"
        "       d.UnitPrice * d.Quantity * (1 - d.Discount) AS LineTotal"
        " FROM OrderDetails d"
        " JOIN Products p ON p.ProductID = d.ProductID"
        " WHERE d.OrderID = ?"
        " ORDER BY p.ProductName", (order_id,)).fetchall()


# --- the reports: aggregate queries across the whole database ---------------

def top_products(con, n=10):
    """ProductName, UnitsSold, Revenue -- best sellers by revenue. GROUP BY the
    product, SUM the money, ORDER BY it descending, keep the top n."""
    return con.execute(
        "SELECT p.ProductName,"
        "       sum(d.Quantity) AS Units,"
        "       sum(d.UnitPrice * d.Quantity * (1 - d.Discount)) AS Revenue"
        " FROM OrderDetails d"
        " JOIN Products p ON p.ProductID = d.ProductID"
        " GROUP BY p.ProductID"
        " ORDER BY Revenue DESC"
        " LIMIT ?", (n,)).fetchall()


def sales_by_category(con):
    """CategoryName, Revenue -- three tables joined (details -> products ->
    categories), grouped by category."""
    return con.execute(
        "SELECT c.CategoryName,"
        "       sum(d.UnitPrice * d.Quantity * (1 - d.Discount)) AS Revenue"
        " FROM OrderDetails d"
        " JOIN Products   p ON p.ProductID  = d.ProductID"
        " JOIN Categories c ON c.CategoryID = p.CategoryID"
        " GROUP BY c.CategoryID"
        " ORDER BY Revenue DESC").fetchall()


def sales_by_employee(con):
    """EmployeeName, Orders, Revenue -- who sold how much. Counts each
    employee's orders and sums their value across three joined tables."""
    return con.execute(
        "SELECT e.FirstName || ' ' || e.LastName AS Employee,"
        "       count(DISTINCT o.OrderID) AS Orders,"
        "       sum(d.UnitPrice * d.Quantity * (1 - d.Discount)) AS Revenue"
        " FROM Employees e"
        " JOIN Orders o       ON o.EmployeeID = e.EmployeeID"
        " JOIN OrderDetails d ON d.OrderID    = o.OrderID"
        " GROUP BY e.EmployeeID"
        " ORDER BY Revenue DESC").fetchall()


def low_stock(con, threshold=15):
    """ProductName, UnitsInStock, SupplierName -- items at or below the
    threshold, with who to reorder from. A WHERE filter plus a join."""
    return con.execute(
        "SELECT p.ProductName, p.UnitsInStock, s.CompanyName"
        " FROM Products p"
        " JOIN Suppliers s ON s.SupplierID = p.SupplierID"
        " WHERE p.UnitsInStock <= ?"
        " ORDER BY p.UnitsInStock", (threshold,)).fetchall()
