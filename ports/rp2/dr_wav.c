// dr_wav single-header WAV decoder (public domain, David Reid / mackron),
// vendored from MMBasic's third_party_mod. This TU compiles the implementation;
// audio.c includes only the declarations and drives it via stream callbacks.
// Matches MMBasic's configuration (no stdio, no SIMD).

#define DR_WAV_NO_STDIO
#define DR_WAV_NO_SIMD
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
