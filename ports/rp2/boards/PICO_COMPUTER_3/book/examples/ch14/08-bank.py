class Account:
    def __init__(self, owner, balance=0):
        self.owner = owner
        self.balance = balance

    def deposit(self, amount):
        if amount <= 0:
            raise ValueError("deposits must be positive")
        self.balance += amount

    def withdraw(self, amount):
        if amount <= 0:
            raise ValueError("withdrawals must be positive")
        if amount > self.balance:
            raise ValueError(f"only {self.balance} available")
        self.balance -= amount

    def __str__(self):
        return f"{self.owner}: {self.balance} credits"
