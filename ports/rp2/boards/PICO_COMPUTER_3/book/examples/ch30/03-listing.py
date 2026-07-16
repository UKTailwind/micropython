import ulab.numpy as np

x = np.linspace(0, 2 * math.pi, 128)    # 128 evenly spaced values
y = np.sin(x)                           # sin of ALL of them, at once
plot(y, x=x)
