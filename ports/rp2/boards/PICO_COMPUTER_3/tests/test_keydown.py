# INTERACTIVE: key-state input (keydown / on_key). Needs a USB keyboard and
# a human to press keys as prompted.

import keyboard
import time
import testutil as T


def wait_key(timeout_ms=10000):
    t0 = time.ticks_ms()
    while keyboard.keydown(0) == 0:
        if time.ticks_diff(time.ticks_ms(), t0) > timeout_ms:
            return False
        time.sleep_ms(10)
    return True


def wait_release(timeout_ms=10000):
    t0 = time.ticks_ms()
    while keyboard.keydown(0):
        if time.ticks_diff(time.ticks_ms(), t0) > timeout_ms:
            return False
        time.sleep_ms(10)
    return True


T.section("keydown (interactive)")

T.prompt("\n== keydown test: use the USB keyboard ==")
T.prompt("1) Press and HOLD the A key...")
if wait_key():
    T.check(keyboard.keydown(1) == ord("a"), "held A reads as 'a' (97)")
    T.prompt("   ...now release it")
    wait_release()
    T.check(keyboard.keydown(0) == 0, "release clears the key state")
else:
    T.check(False, "timed out waiting for A")

T.prompt("2) HOLD Shift and press/hold A...")
if wait_key():
    T.check(keyboard.keydown(1) == ord("A"), "Shift-A reads as 'A' (65)")
    T.check(keyboard.keydown(7) & 0x88 != 0, "shift bit set in keydown(7)")
    T.prompt("   ...release both")
    wait_release()

T.prompt("3) Press and HOLD the UP arrow...")
if wait_key():
    T.check(keyboard.keydown(1) == keyboard.UP, "arrow reports keyboard.UP")
    T.prompt("   ...release")
    wait_release()

T.prompt("4) HOLD A and D together...")
if wait_key():
    time.sleep_ms(400)
    n = keyboard.keydown(0)
    codes = {keyboard.keydown(1), keyboard.keydown(2)}
    T.check(n == 2, "two keys held reports 2 (got %d)" % n)
    T.check(codes == {ord("a"), ord("d")}, "both codes reported")
    T.prompt("   ...release both")
    wait_release()

T.section("on_key (interactive)")
got = []
keyboard.on_key(lambda c: got.append(c))
T.prompt("5) Type the letter x once...")
t0 = time.ticks_ms()
while ord("x") not in got and time.ticks_diff(time.ticks_ms(), t0) < 10000:
    time.sleep_ms(20)
keyboard.on_key()  # remove callback
T.check(ord("x") in got, "on_key delivered the keypress")
keyboard.keydown(0)  # drain the typed-ahead input

if __name__ == "__main__":
    T.report()
