// Pico Computer 3 emulator variant. Same feature level as the standard unix
// build, plus what the board's frozen modules expect from the firmware.

// Set base feature level.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// Enable extra Unix features.
#include "../mpconfigvariant_common.h"

// The board's graphics stack (pcgfx.Display) wraps framebuf.
#define MICROPY_PY_FRAMEBUF (1)

// The REPL banner names the machine, as the firmware's does.
#define MICROPY_BANNER_MACHINE "PICO COMPUTER 3 v0.8 emulator"
