# TinyUSB 0.20 → 0.21: every change, both firmwares

The Pico Computer 3's MicroPython firmware (v0.17) and the Fuzix kernel
(v0.26, kernel asset updated 2 September) both moved their USB host stack
from TinyUSB 0.20.0 to 0.21.0 on 2026-09-01. This document identifies
every change that move required. The long-form investigation, with the
measurements and the event logs, is DEVELOPMENT_NOTES.md §73; the Fuzix
side's panic analysis is in the FUZIX tree,
`Kernel/platform/platform-rpipico/PC3-IRQ-REVIEW.md`.

Commits:

| Tree | Commit | What |
|---|---|---|
| micropython `pico-computer-3` | `9e664962a` | the 0.21 move, hcd RX-timeout fix, MULTI_HUB_FIX |
| micropython `pico-computer-3` | `92184df46` | the other two host fixes + event queue + debug tooling |
| FUZIX `pc3` | `a4a4992c7` | the 0.21 move, hcd RX-timeout fix, panic hook (shipped in v0.26) |
| FUZIX `pc3` | `6ba513662` | the other two host fixes + event queue (v0.26's updated kernel asset) |

(The USB flash-drive feature — `usbdrive`, `/usb`, `CFG_TUH_MSC` — landed
between these commits (`669af8345`) but is a feature, not part of the
version move, and is not covered here.)

## 1. Why the move

TinyUSB 0.20's rp2 host driver asks the root port its speed
(`hcd_port_speed_get()`) at the start of **every** transfer and panics
`"Invalid speed"` when `SIE_STATUS.SPEED` reads 0 — disconnected. A hub
and keyboard powering up together genuinely drop the link for a moment
while enumerating, so a power-on boot with a keyboard attached was an
occasional hard fault — a field report against Fuzix v0.25, and the same
file is compiled into the MicroPython firmware. The drop itself is 0.20's
hcd hammering a NAKing device every 16 µs with no way to yield EPX. A
one-line fix of 0.20 made a working board worse, and a bisect showed the
0.20 outcome was timing-dependent per build — not fixable by a line.

0.21 rewrote that driver: EPX switching uses RP2350's `STOP_EPX_ON_NAK`,
NAK polling is 300 µs, a disconnected port answers `TUSB_SPEED_INVALID`
instead of panicking, and the host stack no longer blocks inside
`tuh_task()`. It is also the version MicroPython v1.29.0 actually pins —
our v1.29.0 merge had kept the older 0.20 pin for parity with the other
firmwares on this machine, a reason that vanished the day Fuzix moved.

0.21 as shipped, however, did **not** work on this hardware: it brought
up one device behind the hub where 0.20 brought up three. Three faults
in its host stack had to be found and fixed (§4); all three are filed
upstream (§7).

## 2. Where 0.21 comes from

**MicroPython** (`9e664962a`):

- `.gitmodules`: `lib/tinyusb` URL changed
  `https://github.com/hathach/tinyusb` →
  `https://github.com/micropython/tinyusb.git` (upstream moved to its own
  fork in August 2026).
- `lib/tinyusb` submodule pinned to `b549ac1d8` — tag
  `0.21.0-micropython1`, exactly what upstream v1.29.0 pins.
- `ports/rp2/CMakeLists.txt` asserts at configure time that
  `TUSB_VERSION_MINOR` is 21 and stops with a `FATAL_ERROR` otherwise —
  the submodule drifted silently under a merge once (that is how v0.16
  shipped 1.29.0's glue on a 0.20 stack) and must not again.

**Fuzix**: the kernel builds against the pico-sdk's bundled TinyUSB, so
the move is a checkout: `$PICO_SDK_PATH/lib/tinyusb` at tag `0.21.0`
(hathach). The recipe is in `BUILDING-PC3.md`, and `usbcheck.sh` asserts
the version the same way the MicroPython CMake does. Note the two trees
therefore build from *different* 0.21 checkouts (micropython's fork tag
vs hathach's release tag); the patched files are identical in both.

**New build dependency, both trees**: `patch(1)` (§3).

## 3. The build-time patch machinery

The three fixed files live in TinyUSB's own tree — a submodule in
MicroPython, the SDK's checkout for Fuzix — so they cannot be edited in
place, and a hand-kept copy would drift. Instead, in both
`ports/rp2/CMakeLists.txt` (micropython) and
`Kernel/platform/platform-rpipico/CMakeLists.txt` (FUZIX):

- a `foreach` over `portable/raspberrypi/rp2040/hcd_rp2040`,
  `portable/raspberrypi/rp2040/rp2040_usb` and `host/usbh` **removes each
  file from `tinyusb_host_base`'s `INTERFACE_SOURCES`** (with a
  `FATAL_ERROR` if the file is no longer listed where expected);
- an `add_custom_command` generates `<name>_pc3.c` in the build directory
  with `patch -s -o <out> <src> <patchfile>` and compiles that instead.
  If TinyUSB ever changes under a hunk, `patch` refuses and the build
  stops **at the patch step**, not with a silently misapplied fix;
- the patched copies need the driver's own include directories
  (`src/portable/raspberrypi/rp2040` for `rp2040_usb.h`, `src/host` for
  usbh's private headers) and the same warning suppressions the SDK gives
  the originals (`-Wno-conversion` on the hcd; `-Wno-conversion
  -Wno-stringop-overflow -Wno-array-bounds` on `rp2040_usb.c`).

Patch files: `ports/rp2/{hcd_rp2040,rp2040_usb,usbh}.patch` in
micropython, `Kernel/platform/platform-rpipico/usb/` in FUZIX.

**Fuzix-only trap**: the kernel also links the *device* stack
(`tinyusb_device`), and `rp2040_usb.c` is shared between the two, so it
must additionally be filtered out of `tinyusb_device_base`'s
`INTERFACE_SOURCES` — otherwise the unpatched original links in beside
the patched copy and the build fails with
`multiple definition of 'rp2usb_xfer_continue'`.

## 4. The three fixes to 0.21's host stack

All three were found on this machine (keyboard + flash drive + touch
panel behind the on-board hub), each fixed where it lives, and proven
over 8/8 boots including a cold start. Identical patches in both trees.

**4.1 `hcd_rp2040.patch` — clear the EPX buffer when an RX timeout fails
a transfer.** 0.21's hcd fails a transfer on an RX timeout *without
clearing the EPX buffer control it armed*; the retry's first
`bufctrl_write32` then refuses the stale AVAIL bit and panics
`"buf_ctrl already available"`. On Fuzix this stopped the host coming up
at all; the diagnosing panic hook (§6) showed EPX still holding a
zero-length DATA1 IN — a status stage — with AVAIL set. 0.20 never met
this because it only cleared the timeout bit and let the SIE keep
retrying. The fix is one line: zero `usbh_dpram->epx_buf_ctrl` before
completing the transfer as FAILED.

**4.2 `rp2040_usb.patch` — single-buffer host CONTROL transfers.** A
525-byte `GET_DESCRIPTOR(report)` IN from the touch panel — nine
full-speed packets, double-buffered on EPX — was completed by the driver
after its first 64 bytes with the other buffer still armed; the status
stage then hit the same `"buf_ctrl already available"` panic. Any device
with a HID report descriptor over 128 bytes (some gaming keyboards)
triggers it. The fix narrows `force_single` from
`interrupt_num > 0` to `interrupt_num > 0 || tu_edpt_number(ep_addr) == 0`
— control joins interrupt as single-buffered, while BULK keeps its proven
double-buffering (the flash drive still reads at the wire ceiling,
~1.1 MB/s). Control transfers are small; the cost is nil.

**4.3 `usbh.patch` — `ENUM_RESET_RECOVERY_DELAY_MS` 10 → 100.** This was
*the* multi-device fix. After a hub-port reset, usbh waits the USB 2.0
minimum T(RSTRCY) of 10 ms and sends the first SETUP to address 0. The
drive and the panel are not answering yet; the controller's retries raise
RX_TIMEOUT every ~17 µs, the 0.21 hcd fails the transfer on the *first*
timeout (0.20 tolerated them indefinitely, which is why it never showed),
and a failed enumeration is **never retried** — the device is permanently
absent. The event log showed seven RX timeouts in 120 µs, four FAILs,
then the stack moving on. Every debug-print build "fixed" it by accident,
purely by stretching this gap. 100 ms per freshly reset device costs
+90 ms each at enumeration — under a second even for a loaded hub — and a
device not ready after 100 ms is a device to replug. (Tolerating RX
timeouts inside the hcd instead was tried first and hung the RP2350 on
hot-attach; the wait belongs in usbh.)

## 5. Configuration and API changes

**`CFG_TUH_TASK_QUEUE_SZ` 16 → 64** (micropython
`shared/tinyusb/tusb_config.h`, FUZIX `tusb_config.h`). A completion the
ISR cannot queue is dropped *silently* in a release build and the stack
waits forever for it — seen once in the burst of a three-device power-up
as a completed-but-unconsumed status stage. 64 entries is 768 bytes.

**`tuh_init(0)` → `tuh_rhport_init(0, &rh_init)`** with
`{ .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO }`
(`ports/rp2/mp_usbh.c`). 0.21 keeps `tuh_init()` only as a deprecated
inline wrapper, and this port builds with `-Werror`, so the first 0.21
build stopped on exactly that line.

**`LINESTATE_TUNING.MULTI_HUB_FIX`** set at host init on RP2350 (both
firmwares) — the controller's own silicon fix for turnaround timeouts
through a hub, off by default; the on-board hub is always in the path
here.

**`CFG_TUH_HID_SET_PROTOCOL_ON_ENUM`** is new in 0.21 and its default
matches the behaviour this port already relied on (no automatic
boot-protocol switch); `usb_touch.c` still switches its interfaces to
report protocol itself. No change was needed — noted so nobody "tidies"
it later.

**`#include <stdarg.h>` before `py/runtime.h`** in `mp_usbh.c`:
`mpprint.h` declares `mp_vprintf` only once `va_start` exists, and the
debug hooks (§6) need it.

## 6. Debug instrumentation added along the way (all opt-in, all still in the tree)

- **`PC3_USB_TRACE=1`** (`make BOARD=PICO_COMPUTER_3 PC3_USB_TRACE=1`,
  optionally `PC3_USB_TRACE_LEVEL=1`): TinyUSB's own enumeration/hub/
  class-driver trace. `CFG_TUSB_DEBUG` arrives on the compiler command
  line from TinyUSB's `family.cmake` via the CMake variable `LOG`, so
  `ports/rp2/CMakeLists.txt` sets `LOG` *before* `pico_sdk_init()` — a
  header cannot override a `-D`. The output goes through a raw UART
  writer, **not** `mp_printf`: printing via dupterm re-enters the VM
  inside `tuh_task()` and wedges the board. `ports/rp2/Makefile` passes
  the flags through to CMake. The same switch exists in the Fuzix kernel
  (`usbtrace.c`).
- **`PC3_USB_EVLOG=1`**: a timing-neutral in-RAM event ring (256 × 20 B)
  inside the patched driver — transfer starts `S`, buffer completions
  `C`, hcd interrupts `I`, completions to the stack `X`, timestamped with
  `time_us_32()` — read back over the REPL from the ELF's `pc3_evlog`
  symbol. This existed because every print-based instrument *cured* the
  fault it was hunting (§4.3); four stores per event do not.
- **Fuzix panic hook**: TinyUSB's `panic()` is Fuzix's via `mangle.h` and
  drops its arguments, so `tusb_config.h` routes it to `pc3_usb_panic()`
  (in `usbkbd.c`), which prints the argument, the register it points at,
  the caller, and the SIE/buffer/endpoint state before panicking. This is
  what turned `"buf_ctrl already available"` from a string into §4.1.
  (`config.h` includes `tusb_config.h` for CFG values, guarded by
  `PC3_CONFIG_H_INCLUDES_TUSB` so the hook's `#define panic` and
  `mangle.h` do not collide.)

## 7. Upstream status

Filed against hathach/tinyusb 2026-09-01, one per fault, each with the
measurements:

- [#3874](https://github.com/hathach/tinyusb/issues/3874) — rp2 hcd:
  RX-timeout failure path leaves `epx_buf_ctrl` armed; panic on retry (§4.1)
- [#3875](https://github.com/hathach/tinyusb/issues/3875) — rp2 hcd:
  host control IN of 3+ packets on double-buffered EPX completes after
  the first packet (§4.2)
- [#3876](https://github.com/hathach/tinyusb/issues/3876) — usbh: 10 ms
  reset recovery + hcd fail-fast + no enumeration retry leaves devices
  behind a hub permanently absent (§4.3)

MULTI_HUB_FIX on RP2350 hosts is a further upstream candidate (not yet
filed). Until the issues are fixed upstream, the patches re-apply
mechanically on any TinyUSB update — and refuse loudly if the code
underneath them changes (§3).

## 8. How it was proven

MicroPython: eight consecutive boots — including a cold start — with
keyboard, flash drive and touch panel on the hub, all three enumerating
every time; the keyboard typing; the drive mounting at `/usb` and
reading at ~1.1 MB/s; the panel's 525-byte descriptor fetched (the §4.2
path). Firmware text grew 568 bytes over the 0.20 build. Fuzix: clean
boot with all three devices attached, keyboard typing,
`INT_EP_CTRL = 0x3E` (five interrupt pipes — the panel enumerated, so
its long descriptor went through the patched driver); kernel
`__bss_end__` +536 bytes. On both, a power-on boot may print
`USB keyboard attached` / `detached` / `attached` in a row: that is the
real link drop that used to be the 0.20 panic, now handled by
re-enumeration.

## 9. Hardening on top — the PicoMite validation (2026-09-02)

The PicoMite tree ran its own 0.21 host bring-up on the same hardware and
validated a hardening set across every reset kind
(`PicoMite/docs/usb-host-hardening.html`); its root-cause finding — the RP2
SIE's **single** handshake-result latch, clobbered by interrupt-endpoint
polls ([#3533](https://github.com/hathach/tinyusb/issues/3533)) — explains
the spurious RX timeouts underneath §4. Applied to this port on top of
everything above (DEVELOPMENT_NOTES §75 has the detail):

- `hcd_rp2040.patch` grew an **EP0 RX-timeout grace period** (1 s, window
  closed by any EP0 completion; expiry takes the original fail path with
  the §4.1 buffer clear);
- `usbh.patch` grew **enumeration-exclusive control dispatch** (only
  address 0, the enumerating address and the hub in use may claim the
  control slot mid-enumeration; other traffic waits in the pending FIFO)
  and **hub-port disable on a failed enumeration**
  (`CLEAR_FEATURE(PORT_ENABLE)`, async no-op callback);
- the **mount-callback prints moved out of the callbacks** into a ring
  drained after `tuh_task()` returns (`usb_defer_printf`);
- `CFG_TUH_CONTROL_PENDING_QUEUE_SZ` 4 → 8.

Deliberately not adopted: the #3533 reference driver — on the PicoMite's
marginal rig the strict rewrite enumerated *fewer* devices than the
tolerant set. The §4 patches, `MULTI_HUB_FIX` and the deep event queue all
stand; the PicoMite doc independently confirms the last two.
