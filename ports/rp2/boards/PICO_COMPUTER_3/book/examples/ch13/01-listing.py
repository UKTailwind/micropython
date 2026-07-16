while True:
    reply = input("How old are you? ").strip()
    try:
        age = int(reply)
        break
    except ValueError:
        print(f"'{reply}' isn't a number I know. Digits, please!")
