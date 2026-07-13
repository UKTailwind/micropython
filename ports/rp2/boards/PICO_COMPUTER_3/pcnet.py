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
# SECURITY NOTE: the Wi-Fi SSID and password are stored in PLAINTEXT in
# /settings.json on the flash filesystem (this board has no secure storage).
# Anyone with the board, the SD card image, or a firmware dump can read them.
# Don't save credentials you consider sensitive; use wifi() per-session instead
# (call ntpsync() manually and don't enable auto()).

import time

import pcconfig


def wifi(ssid=None, pw=None):
    """Connect to Wi-Fi. wifi("SSID", "password") saves the credentials and
    connects; wifi() connects using the saved credentials. Returns True on
    success. (Credentials are stored in plaintext — see the module note.)"""
    import network

    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if ssid is not None:  # new credentials: save and force a fresh connect
        pcconfig.set("wifi_ssid", ssid)
        pcconfig.set("wifi_pw", pw or "")
        if wlan.isconnected():
            wlan.disconnect()
            time.sleep_ms(200)
    if wlan.isconnected():
        return True
    ssid = pcconfig.get("wifi_ssid")
    pw = pcconfig.get("wifi_pw", "")
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
    sync from NTP. All failures are swallowed so boot never blocks on Wi-Fi."""
    try:
        if auto() and pcconfig.get("wifi_ssid"):
            ntpsync()
    except Exception:
        pass
