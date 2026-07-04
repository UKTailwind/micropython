# Hot-swappable SD card support for the Pico Computer 3.
#
# This replicates MMBasic's CheckSDCard (FileIO.c): a background poll, running
# about twice a second, that verifies the card is still there and clears down
# the mount if it has been pulled out, so a card can be swapped safely. The
# heavy lifting lives in the C driver (machine.SDCard):
#   * check()  - a lightweight READ_OCR liveness probe, DEFERRED while the card
#                has been accessed in the last ~500ms (so it never disturbs an
#                active transfer, exactly like MMBasic's diskchecktimer).
#   * reinit() - re-identify whatever card is now in the slot (cheap when the
#                slot is empty thanks to the MISO pull-up).
#
# On removal we unmount /sd and warn; on the next poll after a card is inserted
# we re-identify it and mount it again. All of this is driven by a soft
# machine.Timer whose callback is scheduled (not a hard IRQ), so mounting,
# unmounting and printing from it are safe.

import machine
import vfs

_MOUNT = "/sd"
_PERIOD_MS = 500

_sd = None
_tmr = None
_mounted = False
verbose = True  # print a notice when a card is removed / inserted


def mounted():
    return _mounted


def _mount():
    global _mounted
    try:
        vfs.mount(vfs.VfsFat(_sd), _MOUNT)
        _mounted = True
        return True
    except Exception:
        return False


def _umount():
    global _mounted
    try:
        vfs.umount(_MOUNT)
    except Exception:
        pass
    _mounted = False


def _service(t):
    # Runs in scheduler context ~twice a second.
    if _sd is None:
        return
    try:
        if _mounted:
            # Card was present; make sure it still is (deferred by activity).
            if _sd.check() is False:
                _umount()
                if verbose:
                    print("\nWarning: SDcard removed")
        else:
            # No card mounted; pick up one that has just been inserted.
            if _sd.reinit() and _mount():
                if verbose:
                    print("\nSDcard inserted")
    except Exception:
        # Never let a transient error kill the poller.
        pass


def start():
    """Create the SD card, mount it if present, and start the removal poll.
    Returns True if a card was mounted. Safe to call more than once."""
    global _sd, _tmr, _mounted
    if _sd is None:
        _sd = machine.SDCard()
    _mounted = False
    if _sd.present():
        _mount()
    if _tmr is None:
        _tmr = machine.Timer()
        _tmr.init(period=_PERIOD_MS, mode=machine.Timer.PERIODIC, callback=_service)
    return _mounted


def stop():
    """Stop the background poll (leaves any mount in place)."""
    global _tmr
    if _tmr is not None:
        _tmr.deinit()
        _tmr = None
