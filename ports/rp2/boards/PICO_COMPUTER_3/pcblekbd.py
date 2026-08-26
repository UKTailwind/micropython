# BLE keyboard/mouse support for the Pico Computer 3 -- HID over GATT
# (HOGP), the standard Bluetooth LE profile most modern compact/multi-
# device keyboards and mice use. Pure Python: MicroPython's raw
# `bluetooth` module already has everything needed (central scan/connect,
# GATT client, pairing) built in -- no firmware/C changes required for
# this, unlike Bluetooth Classic audio (see btaudio.c). Reports received
# over BLE are fed into the exact same decoders USB keyboards/mice use
# (keyboard.inject_report() / mouse.inject_report()), so keydown(),
# mouse(), num-lock etc. all work identically regardless of transport.
#
# NOT YET TESTED ON REAL HARDWARE -- written against MicroPython's
# documented raw bluetooth API (docs/library/bluetooth.rst) and the
# Bluetooth SIG's standard HOGP characteristic UUIDs, but there is no BLE
# keyboard/mouse in hand yet to verify against. The GATT descriptor
# handle-range bookkeeping in particular (see _start_next_cccd below) is
# the fiddliest part of hand-rolled GATT client code and the most likely
# place a real device's exact attribute layout trips this up.
#
# Known limitations (v1, deliberately kept simple):
#   - One device connected at a time (matches the USB HID model: one
#     logical keyboard, one logical mouse).
#   - No persistent bonding across reboots -- BTstack (used on this board)
#     doesn't support the _IRQ_GET_SECRET/_IRQ_SET_SECRET bonding-store
#     events MicroPython's docs say are "NimBLE only". Re-pairing is
#     needed after every reboot/reconnect. If a device needs numeric-
#     comparison or passkey-entry pairing (not "Just Works"), only numeric
#     comparison is auto-accepted here; passkey entry isn't handled (no
#     input UI wired up for it yet).
#   - Only the fixed-format Boot Keyboard/Mouse Input Report
#     characteristics are used, not arbitrary Report-mode descriptors --
#     covers the large majority of real keyboards/mice (boot mode exists
#     specifically for simple hosts like this), but a device that only
#     implements Report mode won't work without more code.
#   - Uses bluetooth.BLE()'s raw single irq() slot directly (no aioble) --
#     this cannot run at the same time as any code that also calls
#     aioble/ble.irq() elsewhere in the same program, since only one
#     handler can be registered at once. Nothing else in this app uses
#     aioble as of writing, so there's no actual conflict yet.
#
#   import pcblekbd
#   pcblekbd.on_found(lambda addr, addr_type, name, rssi: print(name, rssi))
#   pcblekbd.scan(8000)
#   pcblekbd.on_connect(lambda ok: print("connected" if ok else "disconnected"))
#   pcblekbd.connect(addr, addr_type)

import bluetooth
from micropython import const
import keyboard
import mouse

_IRQ_SCAN_RESULT = const(5)
_IRQ_SCAN_DONE = const(6)
_IRQ_PERIPHERAL_CONNECT = const(7)
_IRQ_PERIPHERAL_DISCONNECT = const(8)
_IRQ_GATTC_SERVICE_RESULT = const(9)
_IRQ_GATTC_SERVICE_DONE = const(10)
_IRQ_GATTC_CHARACTERISTIC_RESULT = const(11)
_IRQ_GATTC_CHARACTERISTIC_DONE = const(12)
_IRQ_GATTC_DESCRIPTOR_RESULT = const(13)
_IRQ_GATTC_DESCRIPTOR_DONE = const(14)
_IRQ_GATTC_WRITE_DONE = const(17)
_IRQ_GATTC_NOTIFY = const(18)
_IRQ_ENCRYPTION_UPDATE = const(28)
_IRQ_PASSKEY_ACTION = const(31)

_IO_CAPABILITY_NO_INPUT_OUTPUT = const(3)
_PASSKEY_ACTION_NUMERIC_COMPARISON = const(4)

_UUID_HID_SERVICE = bluetooth.UUID(0x1812)
_UUID_PROTOCOL_MODE = bluetooth.UUID(0x2A4E)
_UUID_BOOT_KBD_INPUT = bluetooth.UUID(0x2A22)
_UUID_BOOT_MOUSE_INPUT = bluetooth.UUID(0x2A33)
_UUID_CCCD = bluetooth.UUID(0x2902)

_ble = None
_conn = None  # single-connection state dict, or None
_found_cb = None
_connect_cb = None


def _ensure_active():
    global _ble
    if _ble is None:
        _ble = bluetooth.BLE()
    if not _ble.active():
        _ble.active(True)
    _ble.config(io=_IO_CAPABILITY_NO_INPUT_OUTPUT)
    _ble.irq(_irq)


# Minimal AD-structure parser: find the complete/short local name, if any,
# in a scan result's advertising data.
def _name_from_adv(adv_data):
    i = 0
    n = len(adv_data)
    while i + 1 < n:
        length = adv_data[i]
        if length == 0:
            break
        ad_type = adv_data[i + 1]
        if ad_type in (0x09, 0x08) and i + 1 + length <= n:
            return bytes(adv_data[i + 2:i + 1 + length]).decode("utf-8", "ignore")
        i += 1 + length
    return None


def _notify_connect(ok):
    if _connect_cb is not None:
        _connect_cb(ok)


# Kick off (or continue) descriptor discovery for whichever HOGP
# characteristic is next in st["cccd_queue"] -- one at a time, since a
# discovered CCCD isn't otherwise attributable to a particular
# characteristic. Range is [value_handle+1, next_higher_value_handle-1]
# (or the service's end_handle for the last characteristic), computed from
# every characteristic handle seen in the service, not just the ones this
# module cares about -- descriptors belong to whichever characteristic
# declaration precedes them, and other HOGP characteristics (Report Map,
# HID Information, ...) sit between them in the handle space.
def _start_next_cccd(st):
    if not st["cccd_queue"]:
        _notify_connect(True)
        return
    role, value_handle = st["cccd_queue"][0]
    higher = [h for h in st["all_value_handles"] if h > value_handle]
    upper = (min(higher) - 1) if higher else st["hid_end_handle"]
    st["cccd_role"] = role
    st["cccd_found"] = None
    if upper > value_handle:
        _ble.gattc_discover_descriptors(st["conn_handle"], value_handle + 1, upper)
    else:
        # No room for a descriptor -- skip straight to the next target.
        st["cccd_queue"].pop(0)
        _start_next_cccd(st)


def _irq(event, data):
    global _conn
    try:
        if event == _IRQ_SCAN_RESULT:
            addr_type, addr, adv_type, rssi, adv_data = data
            if _found_cb is not None:
                _found_cb(bytes(addr), addr_type, _name_from_adv(adv_data), rssi)

        elif event == _IRQ_SCAN_DONE:
            pass

        elif event == _IRQ_PERIPHERAL_CONNECT:
            conn_handle, addr_type, addr = data
            _conn = {
                "conn_handle": conn_handle,
                "all_value_handles": [],
                "kbd_value_handle": None,
                "mouse_value_handle": None,
                "protocol_mode_handle": None,
                "hid_start_handle": None,
                "hid_end_handle": None,
                "cccd_queue": [],
            }
            _ble.gap_pair(conn_handle)

        elif event == _IRQ_PERIPHERAL_DISCONNECT:
            conn_handle, addr_type, addr = data
            if _conn is not None and _conn["conn_handle"] == conn_handle:
                _conn = None
                _notify_connect(False)

        elif event == _IRQ_ENCRYPTION_UPDATE:
            conn_handle, encrypted, authenticated, bonded, key_size = data
            if encrypted and _conn is not None and _conn["conn_handle"] == conn_handle:
                _ble.gattc_discover_services(conn_handle)

        elif event == _IRQ_PASSKEY_ACTION:
            conn_handle, action, passkey = data
            # "Just Works" pairing needs no response at all. Numeric
            # comparison is auto-accepted (no display to show the number
            # on either side to compare); passkey entry/display actions
            # are left unhandled -- no input UI wired up for those yet.
            if action == _PASSKEY_ACTION_NUMERIC_COMPARISON:
                _ble.gap_passkey(conn_handle, action, 1)

        elif event == _IRQ_GATTC_SERVICE_RESULT:
            conn_handle, start_handle, end_handle, uuid = data
            if _conn is not None and _conn["conn_handle"] == conn_handle and uuid == _UUID_HID_SERVICE:
                _conn["hid_start_handle"] = start_handle
                _conn["hid_end_handle"] = end_handle

        elif event == _IRQ_GATTC_SERVICE_DONE:
            conn_handle, status = data
            if _conn is not None and _conn["conn_handle"] == conn_handle and _conn["hid_start_handle"] is not None:
                _ble.gattc_discover_characteristics(conn_handle, _conn["hid_start_handle"], _conn["hid_end_handle"])

        elif event == _IRQ_GATTC_CHARACTERISTIC_RESULT:
            conn_handle, end_handle, value_handle, properties, uuid = data
            if _conn is None or _conn["conn_handle"] != conn_handle:
                return
            _conn["all_value_handles"].append(value_handle)
            if uuid == _UUID_BOOT_KBD_INPUT:
                _conn["kbd_value_handle"] = value_handle
            elif uuid == _UUID_BOOT_MOUSE_INPUT:
                _conn["mouse_value_handle"] = value_handle
            elif uuid == _UUID_PROTOCOL_MODE:
                _conn["protocol_mode_handle"] = value_handle

        elif event == _IRQ_GATTC_CHARACTERISTIC_DONE:
            conn_handle, status = data
            if _conn is None or _conn["conn_handle"] != conn_handle:
                return
            if _conn["protocol_mode_handle"] is not None:
                # Switch the device into Boot Protocol Mode (0x00) so the
                # fixed-format boot report characteristics are the ones
                # that actually notify. No-op / harmless if the write is
                # rejected -- some devices only ever run in boot mode and
                # don't expose Protocol Mode at all.
                _ble.gattc_write(conn_handle, _conn["protocol_mode_handle"], b"\x00", 1)
            if _conn["kbd_value_handle"] is not None:
                _conn["cccd_queue"].append(("kbd", _conn["kbd_value_handle"]))
            if _conn["mouse_value_handle"] is not None:
                _conn["cccd_queue"].append(("mouse", _conn["mouse_value_handle"]))
            if _conn["cccd_queue"]:
                _start_next_cccd(_conn)
            else:
                _notify_connect(False)  # HID service had neither -- nothing to do

        elif event == _IRQ_GATTC_DESCRIPTOR_RESULT:
            conn_handle, dsc_handle, uuid = data
            if _conn is not None and _conn["conn_handle"] == conn_handle and uuid == _UUID_CCCD:
                _conn["cccd_found"] = dsc_handle

        elif event == _IRQ_GATTC_DESCRIPTOR_DONE:
            conn_handle, status = data
            if _conn is None or _conn["conn_handle"] != conn_handle:
                return
            if _conn["cccd_found"] is not None:
                _ble.gattc_write(conn_handle, _conn["cccd_found"], b"\x01\x00", 1)
            _conn["cccd_queue"].pop(0)
            _start_next_cccd(_conn)

        elif event == _IRQ_GATTC_WRITE_DONE:
            pass

        elif event == _IRQ_GATTC_NOTIFY:
            conn_handle, value_handle, notify_data = data
            if _conn is None or _conn["conn_handle"] != conn_handle:
                return
            if value_handle == _conn["kbd_value_handle"]:
                keyboard.inject_report(bytes(notify_data))
            elif value_handle == _conn["mouse_value_handle"]:
                mouse.inject_report(bytes(notify_data))
    except Exception as e:
        # Never let a malformed event from a flaky device kill the whole
        # BLE stack's callback dispatch.
        print("pcblekbd:", type(e).__name__, e)


def on_found(fn):
    """fn(addr: bytes, addr_type: int, name: str or None, rssi: int), called
    for each device seen during scan()."""
    global _found_cb
    _found_cb = fn


def on_connect(fn):
    """fn(connected: bool), called once HID input reports are actually
    subscribed and ready (not just "BLE connected")."""
    global _connect_cb
    _connect_cb = fn


def scan(duration_ms=8000):
    _ensure_active()
    _ble.gap_scan(duration_ms, 30000, 30000, True)


def stop_scan():
    if _ble is not None:
        _ble.gap_scan(None)


def connect(addr, addr_type=0):
    """addr: 6-byte bytes from on_found(). addr_type: as given by on_found
    (0 = public, 1 = random -- most BLE peripherals use random)."""
    _ensure_active()
    _ble.gap_connect(addr_type, addr)


def disconnect():
    if _conn is not None:
        _ble.gap_disconnect(_conn["conn_handle"])


def connected():
    if _conn is None:
        return False
    return _conn.get("kbd_value_handle") is not None or _conn.get("mouse_value_handle") is not None
