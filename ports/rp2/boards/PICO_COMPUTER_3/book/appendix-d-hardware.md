# Appendix D — Hardware reference

## The machine

RP2350B (two ARM cores at 252 MHz — one runs Python, one paints HDMI;
315/378 MHz via `screen(mode, clock)`) · 512 KB SRAM (framebuffers) ·
8 MB PSRAM (the Python heap) · 16 MB flash (12 MB filesystem at `/`) ·
CYW43 Wi-Fi/Bluetooth on an RM2 module · PCM5102 audio DAC → 3.5 mm
stereo jack · DS3231 battery-backed clock (CR2032) · CH334U 4-port USB
hub for keyboard/mouse/touch · micro-SD slot, hot-swappable.

## Sockets, switches and buttons

| Item | Purpose |
|---|---|
| **USB-C** | power in (2 A supply recommended — the hub passes up to 2 A on) **and** the serial console (CH340 → GP8/9, 115200 8N1) |
| **Power switch** | push-on / push-off, on the USB-C feed |
| **Prog** (micro-USB) | firmware flashing only — with **USB HUB** switch at **DISABLE** |
| **USB HUB switch** | ENABLE = keyboard sockets live; DISABLE = Prog port live |
| **BOOT + RESET** | hold BOOT, click RESET → UF2 drive appears on a PC |
| **HDMI** | to any monitor/TV |
| **SYSTEM I2C** (QWIIC/Qw/ST) | plug-in I2C modules — GP20/21, 3.3 V, 10 K pull-ups fitted |
| **HEARTBEAT jumper** | LED1 source: **CYW43** position (normal — `Pin("LED")`) or GP25 (only for boards built *without* the wireless module — GP25 is the CYW43 chip-select otherwise) |
| **DEBUG header** | SWD (CLK/DAT/GND) — C-level debugging |

## Flashing (chapter 2)

USB HUB switch → DISABLE · connect Prog to PC · hold **BOOT**, click
**RESET** · drag `firmware.uf2` onto the drive that appears · switch
back to **ENABLE**. Files and settings survive reflashing.

## The I/O header

```
GP21  GND        I2C0 SCL (bus shared with QWIIC + DS3231)
GP20  +5V        I2C0 SDA
GP45  GP46   \
GP43  GP44    |  GP40-GP46 are also the ANALOGUE inputs (machine.ADC)
GP41  GP42    |
GP39  GP40   /
GP37  GP38
GP35  GP36
GP26  GP34
GP07  GP06
GP05  GP04
GP03  GP02
GP01  GP00
VCC   VCC        3.3 V out  (logic level: 3.3 V -- 5 V pin powers, never signals)
GND   GND
```

Every GP pin: digital in/out and PWM. **GP32** = DS3231 alarm INT
(open-drain, active low — `ds3231.alarm_pin()` supplies the pull-up);
**GP27** = DS3231 32 kHz output (enable a pull-up to use).

## Reserved pins (unavailable to `machine.Pin`)

GP8/9 console UART · GP12–19 HDMI · GP28/30/31/33 SD card (SPI1) ·
GP10/11/22 audio I2S · GP23/24/25/29 wireless · GP47 PSRAM CS. The
status LED is `Pin("LED")` (on the wireless chip). Full table:
manual §2.

## Screen modes (chapter 15)

| Mode | Pixels | Colours | Clock |
|---|---|---|---|
| `RGB640` (default) | 640×480 | 256 | 252 / 315 / 378 |
| `RGB320` | 320×240 (doubled) | 65,536 + overlay layer | 252 / 315 / 378 |
| `RGB512` | 512×300 (doubled to 1024×600) | 65,536 | 252 |
| `RGB1024` | 1024×600 native | 16 (palette) | 252 |

Allow ~3 s after a mode change for the monitor to lock.

## Notes

- **Serial console:** any terminal at 115200 8N1 on the USB-C port;
  XMODEM transfers ride the same link. Input works even when
  `console("none")` hides output — type `console()` blind to recover.
- **SD cards:** FAT-formatted, hot-swappable, PC-readable — the bridge
  for photos, music and backups.
- **DS3231:** ±2 ppm-class accuracy; CR2032 keeps it through years of
  power-off; the daily alarm survives resets (chapter 28).
- **Wi-Fi credentials** are stored in plain text in `/settings.json`.
