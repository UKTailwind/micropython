# backup.py -- every .py on the flash, to a dated folder on the SD card.
import os

y, mo, d = gettime()[:3]
dest = f"/sd/backup-{y:04}-{mo:02}-{d:02}"
try:
    os.mkdir(dest)
except OSError:
    pass                             # already there today -- refresh it

count = 0
for name in os.listdir("/"):
    if name.endswith(".py"):
        with open("/" + name) as src:
            data = src.read()
        with open(dest + "/" + name, "w") as out:
            out.write(data)
        count += 1
print(f"backed up {count} programs to {dest}")
