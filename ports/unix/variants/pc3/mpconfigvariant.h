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

// The REPL banner names the machine, as the firmware's does. Keep the version
// in step with PICO_COMPUTER_3_VERSION in the board's mpconfigboard.h.
#define MICROPY_BANNER_MACHINE "PICO COMPUTER 3 v0.12 emulator"

// os.dupterm: pcconsole mirrors REPL output to the display window, and the
// emulator's combined stream feeds window keystrokes back into the REPL.
// One slot (all unix_mphal supports).
#define MICROPY_PY_OS_DUPTERM (1)

// SDL build only (PC3EMU_SDL from mpconfigvariant.mk): pump the emulator's
// VM-side work on every event wait -- exactly the machine's pattern, where
// MICROPY_INTERNAL_EVENT_HOOK runs the USB host task. Today it delivers
// pcaudio's consumed-write I2S callbacks (scheduling them from the SDL audio
// thread proved unreliable; the audio thread only flags, the VM delivers).
#ifdef PC3EMU_SDL
extern void pc3emu_event_hook(void);
#define MICROPY_INTERNAL_EVENT_HOOK pc3emu_event_hook()
#endif

// time.sleep() pumps events for its whole duration (the machine's behaviour:
// music keeps playing, timers fire, Ctrl-C lands), instead of the unix
// port's blocking select().
#define MICROPY_UNIX_TIME_SLEEP_EVENT_DRIVEN (1)
