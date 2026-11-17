# network: rp2's network module, as the Pico Computer 3's pcnet and programs use it, on the Colour Maximite 2's own
# Wi-Fi: the firmware's (V8 with the Wi-Fi module; OPTION WIFI, set once at MMBasic's prompt, starts it at power-up and
# joins its network). Through the cmm2 module (the loader's net_ services, version 21). Station only.
#
#   wlan = network.WLAN(network.STA_IF)
#   wlan.active(True)            True if the Wi-Fi is there (OSError if not: no module, or no OPTION WIFI)
#   wlan.connect(ssid, key)      join that network (waits for an address, up to 30 s); OPTION WIFI is not changed
#   wlan.isconnected()           joined, with an address
#   wlan.disconnect(), status(), ifconfig(), config("ssid")
#
# A network joined here stays joined when MicroPython ends; OPTION WIFI's is joined again at power-up or by MMBasic's
# WEB CONNECT.

import cmm2

STA_IF = 0
AP_IF = 1

STAT_IDLE = 0
STAT_CONNECTING = 1
STAT_GOT_IP = 3
STAT_CONNECT_FAIL = -1
STAT_NO_AP_FOUND = -2
STAT_WRONG_PASSWORD = -3

_status = STAT_IDLE


class WLAN:
    IF_STA = STA_IF
    IF_AP = AP_IF

    def __init__(self, interface=STA_IF):
        if interface != STA_IF:
            raise ValueError("the CMM2 joins networks only (STA_IF)")

    def active(self, is_active=None):
        on = cmm2.net_poll() != 0
        if is_active and not on:
            raise OSError("no Wi-Fi: the Wi-Fi module and OPTION WIFI (at MMBasic's prompt) are needed")
        return on

    def connect(self, ssid=None, key=None, *, bssid=None):
        global _status
        if not ssid:
            raise ValueError("connect(ssid, key)")
        self.active(True)
        _status = STAT_CONNECTING
        r = cmm2.net_join(ssid, key or "")
        if r == 0:
            _status = STAT_GOT_IP
        elif r == -13:
            _status = STAT_WRONG_PASSWORD
        elif r == -113:
            _status = STAT_NO_AP_FOUND
        else:
            _status = STAT_CONNECT_FAIL

    def disconnect(self):
        global _status
        cmm2.net_leave()
        _status = STAT_IDLE

    def isconnected(self):
        return cmm2.net_poll() == 2

    def status(self, param=None):
        if param is not None:
            raise ValueError("unknown status parameter")
        if cmm2.net_poll() == 2:
            return STAT_GOT_IP
        return STAT_IDLE if _status == STAT_GOT_IP else _status

    def ifconfig(self, config=None):
        if config is not None:
            raise OSError("set a fixed address with OPTION WIFI")
        return cmm2.ifconfig()

    def config(self, *args, **kwargs):
        if kwargs:
            return None  # (power management, host name...: the firmware's own)
        if args and args[0] == "ssid":
            return cmm2.evaluate("MM.INFO(OPTION SSID)")
        if args and args[0] == "mac":
            return b"\0\0\0\0\0\0"
        raise ValueError("unknown config parameter")

    def scan(self):
        return []  # (MMBasic's WEB SCAN lists them)


def hostname(name=None):
    if name is not None:
        raise OSError("set the host name with OPTION WIFI")
    return cmm2.evaluate("MM.INFO(OPTION SSID)") and "cmm2"


def country(code=None):
    return "XX"
