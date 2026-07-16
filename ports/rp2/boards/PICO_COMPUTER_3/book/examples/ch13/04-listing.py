try:
    with open(path) as f:
        data = f.read()
except OSError as e:
    print(f"Couldn't read {path}: {e}")
