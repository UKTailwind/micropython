while True:
    word = input("A word, or 'stop' to finish: ")
    if word == "stop":
        break
    print(f"'{word}' has {len(word)} letters.")

print("Nice words. Goodbye.")
