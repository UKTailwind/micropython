try:
    points, name = line.strip().split(",")
    scores.append((int(points), name))
except ValueError:
    print("skipping a malformed line")
except OSError:
    print("file trouble -- is the SD card in?")
