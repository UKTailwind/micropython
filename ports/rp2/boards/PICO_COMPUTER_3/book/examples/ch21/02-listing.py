import keyboard

def spy(code):
    print("key:", code)

# ...type at the prompt and watch the report
keyboard.on_key(spy)
