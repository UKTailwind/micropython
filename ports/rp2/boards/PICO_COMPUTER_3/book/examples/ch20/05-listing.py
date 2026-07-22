sound(1, "B", "S", 440)        # voice 1: Sine, Both ears, 440 Hz
# voice 2: Square wave, Left, quieter
sound(2, "L", "Q", 110, 15)
sound(3, "R", "N", 1000)       # voice 3: white Noise, Right
sound(2, "L", "O", 1)          # voice 2 Off
stop()                         # everything off
