# STAND-IN for the USBSerial C class (USB host only): the emulator has no USB
# host, so a USBSerial is never connected. Mirrors the firmware class so book/
# test code that imports or constructs USBSerial still runs.

USB_CDC_MAX = 4


class USBSerial:
    def __init__(self, baudrate=115200, bits=8, parity=None, stop=1, index=0,
                 timeout=0, timeout_char=0):
        self.index = index

    def connected(self):
        return False

    def any(self):
        return 0

    def read(self, n=-1):
        return None

    def readline(self):
        return None

    def readinto(self, buf):
        return None

    def write(self, data):
        return 0

    def flush(self):
        pass

    def txdone(self):
        return True

    def init(self, *args, **kw):
        pass

    def deinit(self):
        pass

    @staticmethod
    def on_change(fn):
        pass
