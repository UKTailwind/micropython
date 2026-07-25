# Wi-Fi + NTP time for the Pico Computer 3.
#
#   wifi("SSID", "password")   connect and remember the credentials
#   wifi()                     connect using the saved credentials
#   tz(1)                      set the timezone offset in hours (e.g. +1 = BST)
#   ntpsync()                  set the clock from an internet time server
#   auto(True)                 sync from NTP automatically at boot
#
# The clock chain is Wi-Fi -> NTP (UTC) -> local time -> the DS3231 RTC, so
# after ntpsync() both the system clock and the battery-backed DS3231 hold
# local time and gettime() reads correctly even offline.
#
# SECURITY NOTE: the saved Wi-Fi password is SCRAMBLED and tied to this board.
# It is XORed with a keystream derived from machine.unique_id() and stored
# base64 in /settings.json, so it is not readable at a glance and will NOT work
# if the settings file is copied to another board (a different unique_id
# decodes to garbage). This is obfuscation, NOT encryption: the board can
# unscramble its own password, so anyone with the board and a REPL can recover
# it. The SSID is stored in plaintext (it is not secret and is handy to see).
# For anything you consider truly sensitive, use wifi("SSID", "pw", save=False)
# per-session (call ntpsync() manually and don't enable auto()).

import time

import pcconfig

# Domain tag folded into the key so this board's unique_id isn't reused raw.
_PW_MAGIC = b"pc3-wifi-v1"


def _pw_keystream(n):
    import hashlib
    import machine

    key = hashlib.sha256(_PW_MAGIC + machine.unique_id()).digest()
    out = b""
    counter = 0
    while len(out) < n:
        out += hashlib.sha256(key + counter.to_bytes(4, "big")).digest()
        counter += 1
    return out[:n]


def _pw_xor(data):
    ks = _pw_keystream(len(data))
    out = bytearray(len(data))
    for i in range(len(data)):
        out[i] = data[i] ^ ks[i]
    return bytes(out)


def _pw_scramble(pw):
    import binascii

    return binascii.b2a_base64(_pw_xor(pw.encode())).decode().strip()


def _pw_unscramble(blob):
    import binascii

    return _pw_xor(binascii.a2b_base64(blob)).decode()


def _store_pw(pw):
    """Save the password in scrambled form and drop any legacy plaintext copy."""
    pcconfig.set("wifi_pw_enc", _pw_scramble(pw or ""))
    pcconfig.unset("wifi_pw")


def _saved_pw():
    """Return the saved password, unscrambling the stored form. A legacy
    plaintext entry (from before scrambling) is migrated in place. '' if none."""
    blob = pcconfig.get("wifi_pw_enc")
    if blob is not None:
        try:
            return _pw_unscramble(blob)
        except Exception:
            return ""
    legacy = pcconfig.get("wifi_pw")  # pre-scrambling plaintext
    if legacy:
        _store_pw(legacy)             # upgrade it to the scrambled form
        return legacy
    return ""


def wifi(ssid=None, pw=None, save=True):
    """Connect to Wi-Fi. wifi("SSID", "password") saves the credentials and
    connects; wifi() connects using the saved credentials. Returns True on
    success. save=False connects with the given credentials WITHOUT saving them
    (the password is otherwise stored scrambled and board-bound — see the
    module note)."""
    import board
    import network

    if not board.has_wifi():
        print("No Wi-Fi hardware on this board (" + board.name() + ")")
        return False

    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    session_pw = None
    if ssid is not None:  # new credentials: (optionally) save and force a fresh connect
        if save:
            pcconfig.set("wifi_ssid", ssid)
            _store_pw(pw or "")
        else:
            session_pw = pw or ""
        if wlan.isconnected():
            wlan.disconnect()
            time.sleep_ms(200)
    if wlan.isconnected():
        return True
    if session_pw is not None:  # save=False: use the given credentials, unsaved
        pw = session_pw
    else:
        ssid = pcconfig.get("wifi_ssid")
        pw = _saved_pw()
    if not ssid:
        print("No Wi-Fi credentials — use wifi('SSID', 'password')")
        return False
    wlan.connect(ssid, pw)
    for _ in range(150):  # wait up to ~15 s for a connection
        if wlan.isconnected():
            break
        time.sleep_ms(100)
    if wlan.isconnected():
        print("Wi-Fi connected:", wlan.ifconfig()[0])
        return True
    print("Wi-Fi connection failed")
    return False


def tz(hours=None):
    """Get or set the timezone offset from UTC in hours (may be fractional,
    e.g. 5.5), persisted. NTP time is UTC; this offset makes the clock local."""
    if hours is None:
        return pcconfig.get("tz", 0)
    pcconfig.set("tz", hours)
    return hours


def ntpsync():
    """Connect to Wi-Fi (if needed), read the time from an internet NTP server,
    apply the saved timezone offset, and set both the system clock and the
    DS3231 RTC to local time. Returns the new time tuple."""
    import ntptime
    import ds3231

    if not wifi():
        raise OSError("Wi-Fi not connected")
    ntptime.settime()  # sets the system RTC to UTC
    off = int(float(pcconfig.get("tz", 0)) * 3600)
    lt = time.localtime(time.time() + off)  # UTC epoch + offset -> local tuple
    # Write local time to the DS3231 (and, via settime, the system clock too).
    ds3231.settime(lt[0], lt[1], lt[2], lt[3], lt[4], lt[5])
    return ds3231.gettime()


def auto(on=None):
    """Get or set whether ntpsync() runs automatically at boot (persisted).
    When on, boot connects Wi-Fi and syncs the clock (adding a few seconds to
    start-up); it falls back silently to the DS3231's battery-backed time if
    Wi-Fi or NTP is unavailable."""
    if on is None:
        return bool(pcconfig.get("ntp_auto", False))
    pcconfig.set("ntp_auto", bool(on))
    return bool(on)


def boot_sync():
    """Called by _boot_board: if auto() is enabled and credentials are saved,
    sync from NTP. All failures are swallowed so boot never blocks on Wi-Fi.
    A board with no radio fitted skips it silently."""
    try:
        import board

        if not board.has_wifi():
            return
        if auto() and pcconfig.get("wifi_ssid"):
            ntpsync()
    except Exception:
        pass
