/*
 * MicroPython for the Colour Maximite 2 G2: a VFS on the loader's file services (vfs_cmm2.c, vfs_cmm2_file.c), so the
 * firmware's FatFs reads and writes the SD card and the USB drive.
 *
 * The MIT License (MIT), as MicroPython.
 */
#ifndef MICROPY_INCLUDED_CMM2_VFS_CMM2_H
#define MICROPY_INCLUDED_CMM2_VFS_CMM2_H

#include "py/obj.h"

extern const mp_obj_type_t mp_type_vfs_cmm2;
extern const mp_obj_type_t mp_type_vfs_cmm2_fileio;
extern const mp_obj_type_t mp_type_vfs_cmm2_textio;

mp_obj_t mp_vfs_cmm2_new(const char *drive);                    // a VFS on "A:", "B:" or "C:"
mp_obj_t mp_vfs_cmm2_file_open(const mp_obj_type_t *type, const char *path, mp_obj_t mode_in);
int mp_vfs_cmm2_errno(int r);                                   // a loader status (minus a FRESULT) as an errno

#endif
