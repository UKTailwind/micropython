#!/bin/bash
# Build the distributable emulator tarball: pc3emu-linux-x64.tar.gz
# (binary + launcher + README + the book's examples as an SD seed).
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../../../.." && pwd)
board="$top/ports/rp2/boards/PICO_COMPUTER_3"

make -C "$top/ports/unix" VARIANT=pc3 -j"$(nproc)"

dist="$here/dist/pc3emu-linux-x64"
rm -rf "$here/dist"
mkdir -p "$dist/sd-seed"

cp "$top/ports/unix/build-pc3/micropython" "$dist/pc3emu-bin"
cp "$here/README.md" "$dist/README.md"

# SD seed: the course book's example programs + the reader's toolkit.
cp -r "$board/book/examples" "$dist/sd-seed/examples"

# Launcher: run from anywhere; seed the SD card on first run.
cat > "$dist/pc3emu" <<'EOF'
#!/bin/sh
set -e
here=$(dirname "$(realpath "$0")")
home="${PC3EMU_HOME:-$HOME/.pc3emu}"
if [ ! -d "$home" ] && [ -d "$here/sd-seed" ]; then
    mkdir -p "$home/flash" "$home/sd"
    cp -r "$here/sd-seed/." "$home/sd/"
    echo "pc3emu: seeded the SD card with the book's examples ($home/sd)"
fi
PC3EMU_HOME="$home" exec "$here/pc3emu-bin" -X heapsize=8m -i -c "import emuboot"
EOF
chmod +x "$dist/pc3emu"

tar -C "$here/dist" -czf "$here/dist/pc3emu-linux-x64.tar.gz" pc3emu-linux-x64
ls -la "$here/dist/pc3emu-linux-x64.tar.gz"
echo "dist ready: unpack anywhere, run ./pc3emu-linux-x64/pc3emu"
