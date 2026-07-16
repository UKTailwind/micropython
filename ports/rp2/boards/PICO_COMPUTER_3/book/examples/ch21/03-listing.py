import time
while True:
    if touch("SWL"):
        print("swiped left")
    if touch("TAP"):
        print("tap at", touch("X"), touch("Y"))
    time.sleep_ms(20)
