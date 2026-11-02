// SQLite's pool on the CMM2 (lib/usqlite's MEMSYS5_HEAP_SIZE and the page cache it sizes), chosen at start-up by the
// memory the loader gives: main.c's cmm2_sqlite_pool. Included in every file by the Makefile, ahead of
// usqlite_config.h's defaults. The cache is that header's rule: an eighth of the pool, at most 256 KB.
#ifndef CMM2_SQLITE_H
#define CMM2_SQLITE_H
#ifndef __ASSEMBLER__
extern int cmm2_sqlite_pool;
#endif
#define MEMSYS5_HEAP_SIZE         cmm2_sqlite_pool
#define SQLITE_DEFAULT_CACHE_SIZE (cmm2_sqlite_pool / 8 < 256 * 1024 ? -(cmm2_sqlite_pool / 8 / 1024) : -256)
#endif
