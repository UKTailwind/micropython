// Pico Computer 3 emulator variant. Same feature level as the standard unix
// build, plus what the board's frozen modules expect from the firmware.

// Set base feature level.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// Enable extra Unix features.
#include "../mpconfigvariant_common.h"

// The board's graphics stack (pcgfx.Display) wraps framebuf.
#define MICROPY_PY_FRAMEBUF (1)

// Match the firmware's allocator signature: the board builds with plain
// m_free(ptr), and the shared hdmi.c calls it that way. (MEM_STATS requires
// the sized allocator, so it goes too -- micropython.mem_*() like the board.)
#undef MICROPY_MALLOC_USES_ALLOCATED_SIZE
#define MICROPY_MALLOC_USES_ALLOCATED_SIZE (0)
#undef MICROPY_MEM_STATS
#define MICROPY_MEM_STATS (0)

// The REPL banner names the machine, as the firmware's does.
#define MICROPY_BANNER_MACHINE "PICO COMPUTER 3 v0.8 emulator"

// os.dupterm: pcconsole mirrors REPL output to the display window, and the
// emulator's combined stream feeds window keystrokes back into the REPL.
// One slot (all unix_mphal supports).
#define MICROPY_PY_OS_DUPTERM (1)
