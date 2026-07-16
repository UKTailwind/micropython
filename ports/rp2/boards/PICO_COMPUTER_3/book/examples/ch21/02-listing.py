import keyboard

def spy(code):
    print("key:", code)

keyboard.on_key(spy)      # ...type at the prompt and watch the report
