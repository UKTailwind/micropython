def add_entry():
    text = input("Today: ").strip()
    if text:
        with open("diary.txt", "a") as f:
            f.write(text + "\n")
        print("Kept.")

def read_diary():
    print("-" * 30)
    with open("diary.txt") as f:
        for i, line in enumerate(f):
            print(f"{i + 1:3}  {line.strip()}")
    print("-" * 30)

while True:
    choice = input("(w)rite, (r)ead or (q)uit? ").strip().lower()
    if choice == "w":
        add_entry()
    elif choice == "r":
        read_diary()
    elif choice == "q":
        break
