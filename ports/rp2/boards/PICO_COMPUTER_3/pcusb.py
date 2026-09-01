# USB flash drive support for the Pico Computer 3.
#
# A drive plugged into the USB host port is mounted at /usb and unmounted
# again when it is pulled - MMBasic's C: drive. Unlike pcsd.py (the SD card,
# which has to be polled), this is event-driven: the USB stack reports the
# mount and the removal, usbdrive.on_change() schedules the handler, and the
# handler runs in the VM, so mounting, unmounting and printing from it are
# safe. The block device is usbdrive.Drive (usb_msc.c / usb_msc_mod.c).
#
# FAT32 only (exFAT is not built into this firmware, as for the SD card): a
# large stick as sold is usually exFAT and must be reformatted FAT32 first.
import vfs
import usbdrive
import machine

_MOUNT = "/usb"
_RETRIES = 3        # a stick can be slow to answer its first reads
_RETRY_MS = 300
_mounted = False
_retry_tmr = None
_retries_left = 0
verbose = True  # print a notice when a drive is mounted / removed


def mounted():
    return _mounted


def _announce():
    if verbose:
        i = usbdrive.info()
        mb = (i[0] * i[1]) >> 20 if i else 0
        print("\nUSB drive mounted at %s (%d MB)" % (_MOUNT, mb))


def _mount():
    # Returns True mounted, False failed (caller decides whether to retry).
    global _mounted
    try:
        vfs.mount(vfs.VfsFat(usbdrive.Drive()), _MOUNT)
        _mounted = True
        return True
    except Exception:
        return False


def _retry(t):
    # Soft-timer callback, scheduled: runs in the VM, outside the USB stack.
    global _retries_left
    if _mounted or not usbdrive.present():
        return
    if _mount():
        _announce()
    elif _retries_left > 0:
        _retries_left -= 1
        _retry_tmr.init(mode=machine.Timer.ONE_SHOT, period=_RETRY_MS, callback=_retry)
    elif verbose:
        print("\nUSB drive: cannot mount %s - is it FAT32?" % _MOUNT)


def _umount():
    global _mounted
    try:
        vfs.umount(_MOUNT)
    except Exception:
        pass
    _mounted = False


def _on_change(connected):
    # Scheduled from the stack, after tuh_task() has returned; runs in the VM.
    global _retries_left, _retry_tmr
    if connected:
        if _mounted:
            return
        if _mount():
            _announce()
        else:
            # Try again shortly: the stick may still be settling, or the
            # first read met the stack busy. Three goes, 300 ms apart.
            _retries_left = _RETRIES
            if _retry_tmr is None:
                _retry_tmr = machine.Timer()
            _retry_tmr.init(mode=machine.Timer.ONE_SHOT, period=_RETRY_MS, callback=_retry)
    elif _mounted:
        _umount()
        if verbose:
            print("\nWarning: USB drive removed")


def start():
    """Mount a drive that is already plugged in and follow plug / unplug.
    Returns True if a drive was mounted. Safe to call more than once."""
    usbdrive.on_change(_on_change)
    if usbdrive.present() and not _mounted:
        _mount()
    return _mounted


def stop():
    """Stop following plug / unplug (leaves any mount in place)."""
    usbdrive.on_change(None)
