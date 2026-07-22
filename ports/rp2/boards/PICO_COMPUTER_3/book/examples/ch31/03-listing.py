import ulab.numpy as np

x = np.linspace(0, 2 * math.pi, 128)    # 128 evenly spaced values
# sin of ALL of them, at once
y = np.sin(x)
plot(y, x=x)
