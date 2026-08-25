# Hot-swappable USB flash drive support for the Pico Computer 3.
#
# Unlike the SD card -- a bare SPI card with no card-detect signal, so
# pcsd.py polls it twice a second -- USB enumeration tells the firmware
# definitively when a drive appears or disappears (machine.USBDrive.on_change,
# C side in usb_msc.c). This module just mounts/unmounts /usb in response to
# that event, reading the new state back from USBDrive().present() -- so,
# unlike pcsd.py, there is no timer and no explicit reinit() step.

import machine
import vfs

_MOUNT = "/usb"

_drive = None
_mounted = False
verbose = True  # print a notice when a drive is removed / inserted


def mounted():
    return _mounted


def _mount():
    global _mounted
    try:
        vfs.mount(vfs.VfsFat(_drive), _MOUNT)
        _mounted = True
        if verbose:
            print("\nUSB drive mounted at", _MOUNT)
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


def _on_change():
    # Runs in scheduler context (mp_sched_schedule), like USBSerial.on_change.
    try:
        if _drive.present():
            if not _mounted:
                _mount()
        elif _mounted:
            _umount()
            if verbose:
                print("\nWarning: USB drive removed")
    except Exception:
        # Never let a transient error kill the callback.
        pass


def start():
    """Create the USB drive object, mount it now if one is already plugged
    in, and register the connect/disconnect handler. Returns True if mounted.
    Safe to call more than once."""
    global _drive, _mounted
    if _drive is None:
        _drive = machine.USBDrive()
        machine.USBDrive.on_change(_on_change)
    _mounted = False
    if _drive.present():
        _mount()
    return _mounted


def stop():
    """Unregister the connect/disconnect handler (leaves any mount in place)."""
    if _drive is not None:
        machine.USBDrive.on_change(None)
