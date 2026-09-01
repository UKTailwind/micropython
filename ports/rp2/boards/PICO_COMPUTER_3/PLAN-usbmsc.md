# PLAN: USB flash drives on the Pico Computer 3 (MicroPython)

Written 2026-09-01 from a user request. Status: **implemented the same
day** — see DEVELOPMENT_NOTES §74 for what shipped and the numbers.
The measurement in §3.3 came out far better than §2 predicted: 701 µs
per command, single-block reads at 729 KB/s, eight blocks and up at the
full-speed wire ceiling (1.1–1.19 MB/s), so neither lever A (read-ahead)
nor lever B (interrupt polling) was needed. The one bug met was
reentrancy, not speed (§74). The rest of this document is the review as
written beforehand; it is kept because §1 and §2 explain the design.

The reference is MMBasic's USB drive (`misc/SDCard.c`,
`C:`), which exists on RP2350 builds and is known to be slow; the point
of this document was to say where that speed goes and what this port can
do about it before a line is written.

## 1. What MMBasic does (read 2026-09-01)

* `CFG_TUH_MSC 1` only `#ifdef rp2350`; `misc/SDCard.c` implements FatFs
  physical drive 1 as `C:` (`FileIO.c` knows `A:`/`B:`/`C:`), state in
  `USBDriveStat` (`STA_NOINIT|STA_NODISK` until mounted).
* Mount/unmount are TinyUSB's `tuh_msc_mount_cb` / `tuh_msc_umount_cb`:
  a sound, a message ("USB Flash Drive Connected (addr n)"), the status
  bits; `disk_initialize()` on first access.
* Reads and writes go through a **chunked chain**: at most
  `USB_MSC_MAX_SECTORS_PER_XFER 127` sectors per `tuh_msc_read10` /
  `write10` — because TinyUSB's host transfer length is a `uint16_t`
  (`hcd_edpt_xfer(..., uint16_t buflen)`, so 127 × 512 = 65,024 bytes is
  the biggest command) — the next chunk submitted from the completion
  callback via a `submit_pending` flag, and a wait loop:

      while (!complete) { submit-if-pending; tuh_int_handler(0,false); tuh_task(); timeout }

  with `msc_wait_ready()` spinning on `tuh_msc_ready()` first, four
  retries, a 200 µs back-off on a refused submit and a 1000 ms per-chunk
  timeout. Note the manual `tuh_int_handler()` call in the loop: the
  controller is polled in-line rather than waited on.
* Instrumentation is left in the source and **compiled out**: a latency
  monitor bucketing read-callback latency at <500 µs / ~1 ms / >2 ms, a
  read-pattern monitor (single vs multi vs sequential), an 8-sector
  read-ahead cache (`USB_MSC_READAHEAD_ENABLE 0`), a sequential-run
  prefetch (`SEQ2`, off), a force-single-sector mode, and a
  hot-path-in-RAM option. The author measured, saw ~1 ms per command as
  the class of the floor, and did not ship the cache. That is the
  experience to inherit: **the per-command latency of the host stack is
  the limit, and MMBasic's own access pattern did not reward
  read-ahead.** Whether FatFS-in-MicroPython's pattern does is a
  measurement, below.

## 2. Where the speed goes

* **Wire.** Full speed, 12 Mb/s, 64-byte bulk packets, about 19 per
  1 ms frame: ~1.2 MB/s is the ceiling for anything on this controller.
* **The hcd.** The RP2040/RP2350 host has one hardware endpoint, EPX,
  for every control and bulk transfer (interrupt endpoints have their
  own hardware and are polled by the controller, which is why the
  keyboard does not compete). Every 64-byte packet completes through an
  interrupt; TinyUSB 0.21 double-buffers EPX in host mode
  (`EP_CTRL_DOUBLE_BUFFERED`), so a large transfer is bounded by
  per-packet interrupt work rather than by turnaround. On RP2350 the
  0.21 driver uses `STOP_EPX_ON_NAK` and switches EPX round-robin among
  pending endpoints when the device NAKs — a busy stick hands EPX to
  whoever else is pending (a `USBSerial`, if one is open).
* **Per command.** A SCSI READ(10) is three transfers — CBW out (31
  bytes), the data, CSW in (13 bytes) — each completing through the
  interrupt → `tuh_task()` event queue → class callback. That chain is
  the ~1 ms MMBasic measured. So single-sector traffic tops out around
  512 bytes per millisecond in theory and 200–300 KB/s in practice,
  while a 64 KB command runs at the wire rate.
* **FatFS in MicroPython.** `FF_FS_TINY 1`: partial-sector reads go
  through the one filesystem window, sector by sector. But `f_read` and
  `f_write` transfer sector-aligned whole sectors **directly, multi-
  sector, clipped at the cluster boundary** (`lib/oofatfs/ff.c:3655-3658`,
  `3770-3773`), and `disk_read` passes the count straight to
  `readblocks` (`extmod/vfs_fat_diskio.c`, `mp_vfs_blockdev_read`). So a
  large `f.read()` on a FAT32 stick produces cluster-sized commands —
  16–32 KB, 32–64 sectors — and lands near the wire rate; directory
  walks, FAT-chain lookups, small files and `f.read(512)` produce
  single-sector commands and sit on the latency floor. That split is
  where any cache earns its keep, and it is FatFS's pattern, not
  MMBasic's, that we would be caching for.
* `MICROPY_FATFS_MAX_SS` is already 4096 (for the flash), so a
  4096-byte-sector stick is handled; exFAT is **off** in this port
  (`FF_FS_EXFAT 0`, `MICROPY_FATFS_EXFAT` undefined), so a stick over
  32 GB as sold (exFAT) will not mount — the same constraint the SD card
  has today.

## 3. Design

### 3.1 Files

* `ports/rp2/usb_msc.c` — the TinyUSB glue, the shape of `usb_cdc.c`:
  `tuh_msc_mount_cb` / `umount_cb` (TinyUSB has already run TEST UNIT
  READY and READ CAPACITY by then — `msc_host.c` `config_*` chain,
  `mounted = true` at line 508 — so `tuh_msc_get_block_count/size` are
  valid inside the mount callback); one drive, LUN 0 first (MMBasic's
  scope; `CFG_TUH_MSC_MAXLUN` default 4 leaves multi-LUN for later);
  blocking `usb_msc_read(lba, count, buf)` / `write` with the chunked
  chain (chunk = 65535 / block_size sectors: 127 at 512, 15 at 4096),
  completion checked on `csw.status`, MMBasic's retries and per-chunk
  timeout, ENODEV the moment the device is unmounted mid-chain. The
  wait loop pumps the port's own `mp_usbh_task()` (so `hid_poll` and
  the keyboard keep running through a long copy) via a helper that
  honours the reentrancy guard: if `tuh_task()` is already on the
  stack, the call is refused with EBUSY rather than deadlocked. Mount /
  unmount events reach Python with `mp_sched_schedule`, exactly as
  `usbserial.on_change` and `keyboard.on_usb_event` do.
* `ports/rp2/usb_msc_mod.c` — module `usbdrive`: `Drive(lun=0)` is a
  block device — `readblocks(n, buf)`, `writeblocks(n, buf)`,
  `ioctl(op, arg)` with INIT/DEINIT/SYNC as no-ops, BLOCK_COUNT and
  BLOCK_SIZE from the device, ERASE (6) returning 0 — plus
  `present()`, `info()` (block count, block size, VID:PID) and
  `on_change(cb)`. Registered in `ports/rp2/CMakeLists.txt` under
  `MICROPY_HW_USB_HOST` beside the other `usb_*` sources. Consider the
  native block-device fast path (`MP_BLOCKDEV_FLAG_NATIVE`, a C function
  pointer instead of a Python call per block) if `machine.SDCard` uses
  it; it only matters for single-sector traffic.
* `shared/tinyusb/tusb_config.h` — `CFG_TUH_MSC (1)` in the host
  section. Cost: one `msch_interface_t` per possible device (7 × ~100
  bytes); the enumeration buffer is shared.
* `pcusb.py` (frozen, the `pcsd.py` pattern but **event-driven**, not
  polled — the stack tells us): `start()` registers `on_change`; on
  connect `vfs.mount(vfs.VfsFat(usbdrive.Drive()), "/usb")` and a
  notice with the size; on disconnect `vfs.umount("/usb")` and a
  notice; `verbose`, `mounted()`. `_boot_board.py`: `import pcusb;
  pcusb.start()`. The plug/unplug sounds already fire for any device
  (§24).
* Docs: DEVELOPMENT_NOTES §74, USER_MANUAL "USB flash drives" (mount
  point, FAT32 only, pull-out semantics, the speed numbers), `pcshell`
  / `fm` shown `/usb` if they list mount points.

### 3.2 Decisions

* Mount point `/usb` (MMBasic's `C:`). One drive at a time to start;
  through the hub is fine.
* **Write-through, no write-back cache**: a stick gets pulled. FatFS
  writes are cluster-bounded multi-sector too, so large writes are
  already efficient; `ioctl(SYNC)` is a no-op (SCSI SYNCHRONIZE CACHE is
  optional and most sticks ignore it).
* Removal while mounted: the next block call raises `OSError(ENODEV)`;
  `pcusb` unmounts on the event; open files fail at their next access —
  the same contract as the SD card today.
* Reentrancy: block calls come from Python only. A filesystem call from
  inside `on_change` / `on_usb_event` is fine — those are scheduled and
  run in the VM, outside `tuh_task()`; anything reached from inside
  `tuh_task()` is refused (EBUSY), documented.
* Buffers need no alignment or SRAM residency: the rp2 hcd copies
  packet by packet with the CPU, no DMA, so the user's PSRAM-heap
  `bytearray` is passed straight through and nothing is staged.
* exFAT stays off for now — document "format FAT32". Turning it on is
  `MICROPY_FATFS_EXFAT 1` plus ~10 KB of flash and the licensing note
  MicroPython attaches to it; a separate decision.
* Power: a stick can draw 200–500 mA; whether the PC3 hub's budget
  covers a stick beside a keyboard is a hardware question for the manual
  once measured.

### 3.3 Speed: measure first, then two levers

0. **Benchmark before anything is tuned** — `usbbench.py`: raw
   `readblocks` at 1, 8, 32, 64 and 127 sectors; `f.read(512)`,
   `f.read(4096)`, `f.read(32768)` and `f.readinto()` on a 4 MB file; an
   `os.ilistdir()` of a 500-file directory; a 1 MB write. KB/s each,
   written into the notes. Expectation from §2: 127-sector raw reads
   600–900 KB/s, single-sector 200–300 KB/s, file reads at the cluster
   size in between.
1. **Lever A — read-ahead in the block device (C).** When a one-sector
   request follows the previous one sequentially, fetch N (8–32) and
   serve the run from the cache; invalidate on any write into the
   range, on unmount, and on a non-sequential access. MMBasic has the
   skeleton switched off; we keep it only if the benchmark says so —
   the directory walk and small-file cases are the ones it should move.
   N × 512 bytes of static RAM.
2. **Lever B — poll the controller in the wait loop** (`tuh_int_handler
   (0, false)` before `tuh_task()`, as MMBasic does) and measure the
   per-command latency with and without. 0.21's stack is already
   asynchronous; this may be at the floor.
3. Not levers: `FF_FS_TINY` (irrelevant to the direct multi-sector
   path), `MICROPY_FATFS_MAX_SS` (already 4096), and the hcd itself. We
   do carry a patched copy of the hcd, so a further patch is mechanically
   possible, but per-packet interrupt completion is the driver's
   architecture, not a bug — that is the "SDK driver limitation", and it
   is the same on MMBasic.

### 3.4 Order of work, with gates

1. `CFG_TUH_MSC`, `usb_msc.c`, the module; plug a stick: mount message,
   `usbdrive.Drive().readblocks(0, buf)` shows `0x55AA` at 510.
2. `vfs.mount(...,'/usb')` from the REPL; `os.listdir`; read a file and
   compare it byte for byte with the PC's copy; write one and read it
   back on the PC.
3. `pcusb.py` auto-mount and the events; ten pull-out / insert cycles;
   a 10 MB copy `/usb` → `/sd` with the keyboard being typed on
   throughout; `USBSerial` open at the same time (EPX sharing).
4. The benchmark; levers A and B by measurement; numbers into the notes
   and the manual.
5. Notes §74, manual, and ship with the TinyUSB 0.21 move (v0.17).

### 3.5 Risks

* EPX sharing between a busy stick and an open `USBSerial` under 0.21's
  NAK switching — throughput of both, and starvation; test in step 3.
* Sticks that stay "not ready" for a while after power: TinyUSB's mount
  sequence already loops TEST UNIT READY → REQUEST SENSE → retry, but
  only within its own timing; a slow stick may need the same patience
  MMBasic's `msc_wait_ready` shows.
* MMBasic's counters (`submits_fail`, `mid_submit_fail`,
  `chain_timeout`) say refused submits and timeouts happened in the
  field; the retry path is not optional.
* A long blocking read stalls Python and the VM hook; the keyboard stays
  captured by hardware and drained by the pump in the wait loop, but
  `keyboard.on_key` callbacks and sounds are deferred until the call
  returns. Chunks are ≤ 64 KB, ~60–100 ms each.

Effort: 300–400 lines of C, ~60 of Python, a day with the board, plus
half a day of measurement.
