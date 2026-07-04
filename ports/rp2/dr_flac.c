// dr_flac single-header FLAC decoder (public domain, David Reid / mackron),
// stock copy from https://github.com/mackron/dr_libs. This TU compiles the
// implementation; audio.c includes only the declarations and drives it via
// stream callbacks with a GC-heap allocator. Ogg-FLAC and SIMD are disabled to
// save code/RAM (native .flac only).

#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_OGG
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
