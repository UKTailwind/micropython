# Pico Computer 3 test suite runner.
#
#   run("/sd/tests/test_all.py")
#
# Runs the automatic (self-verifying) suites first, then offers the
# interactive ones (keyboard/audio/console need a human). Each file can also
# be run on its own: run("/sd/tests/test_blit.py").

import sys
import testutil as T

AUTO = ["test_blit.py", "test_buffers.py", "test_sprites.py",
        "test_images.py", "test_fonts.py", "test_math.py", "test_misc.py"]
INTERACTIVE = ["test_keydown.py", "test_audio.py", "test_console.py"]

T.reset()


def run_file(fname):
    print()
    print("=== running", fname, "===")
    try:
        exec(open(fname).read(), {"__name__": "suite"})
    except Exception as e:
        T.check(False, fname + " crashed: " + repr(e))
        sys.print_exception(e)


for f in AUTO:
    run_file(f)

T.restore_screen()
print()
print("Automatic tests done.")
if T.ask("Run the INTERACTIVE tests (keyboard, audio, console)?"):
    for f in INTERACTIVE:
        run_file(f)

T.restore_screen()
T.report()
