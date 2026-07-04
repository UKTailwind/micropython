import vfs
import machine, rp2


# Try to mount the filesystem, and format the flash if it doesn't exist.
# Note: the flash requires the programming size to be aligned to 256 bytes.
bdev = rp2.Flash()
try:
    fs = vfs.VfsLfs2(bdev, progsize=256)
except:
    vfs.VfsLfs2.mkfs(bdev, progsize=256)
    fs = vfs.VfsLfs2(bdev, progsize=256)
vfs.mount(fs, "/")

# Optional board-provided boot hook: a board can freeze a `_boot_board` module
# (via its manifest) to run extra start-up -- mount other filesystems, pre-import
# helpers into the REPL, bring up a display, etc. Boards without one are
# unaffected, so no board-specific logic needs to live in this shared file.
try:
    import _boot_board  # noqa: F401
except ImportError:
    pass

del vfs, bdev, fs
