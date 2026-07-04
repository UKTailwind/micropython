// dr_mp3 single-header MP3 decoder (public domain, David Reid / mackron),
// stock copy from https://github.com/mackron/dr_libs. This TU compiles the
// implementation; audio.c includes only the declarations and drives it via
// stream callbacks with a GC-heap allocator.

#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_MP3_ONLY_MP3
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
