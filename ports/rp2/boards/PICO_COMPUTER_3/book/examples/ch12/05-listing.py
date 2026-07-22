REC = 20                             # 12 name + 7 score + newline

def format_rec(name, points):
    # padding makes EVERY record 20 bytes
    return f"{name:12}{points:7}\n"

# read record number 2 directly -- no loop, no loading the file
with open("scores.dat") as f:
    f.seek(2 * REC)
    line = f.read(REC)
    name, points = line[:12].strip(), int(line[12:19])

# update record number 1 in place, leaving its neighbours
# untouched
with open("scores.dat", "r+") as f:
    f.seek(1 * REC)
    f.write(format_rec("Grace", 999))
