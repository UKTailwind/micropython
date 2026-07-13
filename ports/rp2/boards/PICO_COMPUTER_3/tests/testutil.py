# Shared test framework for the Pico Computer 3 test suite.
#
# Tiny on-device check/report helpers. Graphics tests call quiet() first so
# their prints go to the serial port only (console("serial")) and pixel
# checks aren't corrupted by console output on the HDMI screen; report()
# restores console("both") before printing the summary.

import pcconsole

passed = 0
failed = 0
failures = []
_section = ""


def reset():
    global passed, failed, failures, _section
    passed = 0
    failed = 0
    failures = []
    _section = ""


def section(name):
    global _section
    _section = name
    print("---", name)


def check(cond, name):
    global passed, failed
    if cond:
        passed += 1
        print("  PASS", name)
    else:
        failed += 1
        failures.append(_section + ": " + name)
        print("  FAIL", name)
    return cond


def check_raises(exc, fn, name, *args, **kw):
    try:
        fn(*args, **kw)
    except exc:
        return check(True, name)
    except Exception as e:
        print("   (raised", type(e).__name__, "instead)")
        return check(False, name)
    return check(False, name + " (no exception)")


def quiet():
    # Route console output to serial only: the HDMI screen belongs to the
    # test's graphics, and on-screen prints would corrupt pixel checks.
    pcconsole.console("serial")


def loud():
    pcconsole.console("both")


def prompt(msg):
    # Interactive step: make sure the user can see it on both consoles.
    loud()
    print(msg)


def ask(question):
    """y/n question to the human tester; returns True for y/Y/empty."""
    loud()
    ans = input(question + " [Y/n] ").strip().lower()
    return ans in ("", "y", "yes")


def restore_screen():
    """Return to the user's saved display mode with the console attached."""
    import hdmi
    import pcconfig

    hdmi.deinit()
    try:
        hdmi.init(pcconfig.get("hdmi_mode", hdmi.RGB640), pcconfig.get("hdmi_clock", 252))
    except Exception:
        hdmi.init(hdmi.RGB640)
    pcconsole.console()


def report():
    loud()
    print()
    print("======================================")
    print("PASSED:", passed, "  FAILED:", failed)
    for f in failures:
        print("  FAIL:", f)
    print("======================================")
    ok = failed == 0
    # Reset so a re-run in the same session starts clean. The counters live in
    # module globals (shared across run() calls), and only test_all resets at
    # start; without this, running a single test file twice — or a file and
    # then test_all — would ACCUMULATE and double-report earlier results.
    reset()
    return ok
