# INTERACTIVE: console output routing (console "both"/"serial"/"screen").
# Best run with BOTH a serial terminal and the HDMI screen visible.

import time
import pcconsole
import testutil as T

T.section("console routing (interactive)")
T.prompt("\n== console routing test ==")
T.prompt("Watch BOTH the HDMI screen and the serial terminal.")
time.sleep(2)

pcconsole.console("serial")
print(">>> SERIAL-ONLY marker <<<")
time.sleep(2)
pcconsole.console("screen")
print(">>> SCREEN-ONLY marker <<<")
time.sleep(2)
pcconsole.console("both")
print(">>> BOTH marker <<<")

T.check(T.ask("Serial terminal: SERIAL-ONLY and BOTH markers, but NOT the "
              "SCREEN-ONLY one?"), "serial routing")
T.check(T.ask("HDMI screen: SCREEN-ONLY and BOTH markers, but NOT the "
              "SERIAL-ONLY one?"), "screen routing")
T.check_raises(ValueError, pcconsole.console, "bad target rejected", "nope")

if __name__ == "__main__":
    T.report()
