# Installing the Pico Computer 3 Emulator on Windows

The emulator is a Linux program; on Windows it runs under **WSL2**
(Windows Subsystem for Linux), which is built into Windows and shows the
emulator's windows and plays its sound like any other app. This guide
takes a machine that has never seen WSL to a running emulator. Nothing
here needs prior Linux experience — every command is given in full.

You need Windows 11, or Windows 10 (64-bit, version 21H2 or later), and
about 2 GB of disk space. On Windows 10 one extra step is flagged below.

## Step 1 — Install WSL and Ubuntu

1. Right-click the Start button and choose **Terminal (Admin)** (on
   Windows 10: **Windows PowerShell (Admin)**).
2. Type:

       wsl --install

   This installs WSL2 and the Ubuntu Linux distribution in one go.
3. **Reboot** when it asks.
4. After the reboot a window opens finishing the Ubuntu install. It asks
   for a **username** and **password** — these are new, for Linux only
   (the password doesn't echo as you type; that's normal). Remember
   them: the password is needed for `sudo` (administrator) commands.

You now have an "Ubuntu" entry in the Start menu. Opening it gives a
Linux terminal — every command below is typed **there**, not in
PowerShell.

> **Windows 10 only:** WSL's GUI support (WSLg) comes with the Store
> version of WSL. Run `wsl --update` in the admin terminal and make sure
> `wsl --version` reports a WSLg version. Without it the emulator still
> runs, but terminal-only.

> **Already had WSL?** `wsl --update` then `wsl --install -d Ubuntu` (if
> Ubuntu is missing), and check `wsl -l -v` shows Ubuntu as VERSION 2 —
> if it says 1, run `wsl --set-version Ubuntu 2`.

## Step 2 — First-time Ubuntu setup

In the Ubuntu terminal, refresh the package lists and install the one
library the emulator needs:

    sudo apt update
    sudo apt install -y libsdl2-2.0-0

(It asks for the password you created in step 1.)

## Step 3 — Install the emulator

Download `pc3emu-linux-x64.tar.gz` from the release page
(https://github.com/UKTailwind/micropython/releases) **with your normal
Windows browser** — it lands in your Windows Downloads folder, which
Ubuntu can see at `/mnt/c/Users/<YourWindowsName>/Downloads`.

Unpack it into your Linux home directory:

    cd ~
    tar xzf /mnt/c/Users/<YourWindowsName>/Downloads/pc3emu-linux-x64.tar.gz

(Type your Windows username in place of `<YourWindowsName>`; the Tab key
auto-completes paths as you type.)

## Step 4 — Run it

    ~/pc3emu-linux-x64/pc3emu

Two things appear: the **machine's display window** (boot banner and the
`>>>` prompt — type in it as if at the real keyboard) and the terminal
keeps acting as the **serial console** (everything mirrors there, and
it's the natural place to paste from). On first run the SD card is
seeded with the course book's example programs — try (it asks for an
angle and a step count; 89 and 150 are a good first answer):

    run("/sd/examples/ch09/08-spiro.py")

Exit with **Ctrl-D** at the prompt. Day to day: open Ubuntu from the
Start menu, run the same command. For a one-click start, make a Windows
shortcut with this as the target:

    wsl.exe -d Ubuntu -- ~/pc3emu-linux-x64/pc3emu

## Where your files are

The machine's drives are two ordinary folders in Ubuntu:

    ~/.pc3emu/flash        the flash drive  /      (settings.json lives here)
    ~/.pc3emu/sd           the SD card      /sd

Windows Explorer can see them: paste this in the Explorer address bar —

    \\wsl.localhost\Ubuntu\home\<your-linux-username>\.pc3emu

Drag files in and they appear on the machine instantly (and the machine's
files can be edited with any Windows editor). Those two folders are
yours: **upgrading the emulator never touches them** — to upgrade,
delete `~/pc3emu-linux-x64` and unpack the new tarball in its place.

Copying a program listing in: select and copy it in Windows, click the
emulator window, type `autosave("prog.py")`, press **Ctrl-V**, then
**Ctrl-Z** to save.

## Building from source instead

If you'd rather compile it (or want to track the repository):

    sudo apt install -y git build-essential libsdl2-dev python3
    git clone --branch pico-computer-3 https://github.com/UKTailwind/micropython
    cd micropython
    make -C mpy-cross -j$(nproc)
    make -C ports/unix VARIANT=pc3 submodules
    make -C ports/unix VARIANT=pc3 -j$(nproc)

Run it with:

    ports/rp2/boards/PICO_COMPUTER_3/emulator/pc3emu

## If something doesn't work

- **No display window, only the terminal** — WSLg isn't active. In an
  admin PowerShell: `wsl --update`, then `wsl --shutdown`, reopen
  Ubuntu. `wsl --version` should list a WSLg version (Windows 10: this
  needs the Store version of WSL, which `wsl --update` installs).
- **`error while loading shared libraries: libSDL2`** — step 2 was
  skipped: `sudo apt install -y libsdl2-2.0-0`.
- **No sound** — sound also travels through WSLg, so the fix is the same
  `wsl --update` / `wsl --shutdown` cycle. Check Windows hasn't muted
  the "System sounds" mixer entry for WSLg.
- **`wsl --install` reports virtualisation is disabled** — enable
  "Virtualization Technology" (Intel VT-x / AMD-V) in the PC's BIOS
  setup, and on some machines Windows' "Virtual Machine Platform"
  feature: `dism /online /enable-feature
  /featurename:VirtualMachinePlatform /all /norestart`, then reboot.
- **The window is slow to appear the very first time** — WSLg warms up
  on first use after boot; subsequent starts are quick.
- **Keyboard layout wrong in the window** — it follows the machine's
  setting, exactly as on hardware: `keymap("UK")` (or "US", "DE", "FR",
  "ES", "IT"), saved across runs.

## What the emulator is (and isn't)

It compiles the same drawing, keyboard, audio and Python-toolkit sources
as the firmware, so behaviour — display modes, fonts, colours, the
editor, the file manager, sound, the clock — is the machine's, not an
imitation. What it can't reproduce: real GPIO voltages (the `Pins` panel
simulates the header: `import Pins`), the touch panel, Wi-Fi/Bluetooth
radio quirks, and true hardware timing (it usually runs much faster).
The `README.md` alongside this file covers day-to-day use.
