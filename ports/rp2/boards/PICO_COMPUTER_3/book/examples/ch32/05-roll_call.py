import machine

i2c = machine.I2C(0, sda=Pin(20), scl=Pin(21), freq=400000)
found = i2c.scan()
print("devices answering:", [hex(a) for a in found])
