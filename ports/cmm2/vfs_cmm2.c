/*
 * MicroPython for the Colour Maximite 2 G2: a VFS on the loader's file services. Modelled on extmod/vfs_posix.c
 * (Damien P. George), with the loader's file_open, dir_open, file_stat... in place of POSIX's calls.
 *
 * VfsCMM2("A:") is a drive (A: and B: the SD card, C: the USB drive). Paths given to it are relative to its mount
 * point: "/x" is the drive's /x, and a relative one is from its own current directory (chdir), as FAT's VFS keeps one.
 *
 * The MIT License (MIT), as MicroPython. Copyright (c) 2017-2018 Damien P. George; 2026 the CMM2 port.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/vfs.h"
#include "shared/timeutils/timeutils.h"
#include "cmm2.h"
#include "vfs_cmm2.h"

#define PATH_MAX_CMM2 (MICROPY_ALLOC_PATH_MAX + 8)

typedef struct _mp_obj_vfs_cmm2_t {
    mp_obj_base_t base;
    char drive[3];                  // "A:"
    char cwd[MICROPY_ALLOC_PATH_MAX + 1];   // the current directory, from the drive's root: "/" or "/a/b"
    bool readonly;
} mp_obj_vfs_cmm2_t;

// FatFs's result (as the loader gives it, negated) to an errno
int mp_vfs_cmm2_errno(int r) {
    switch (-r) {
        case 4:                     // FR_NO_FILE
        case 5:                     // FR_NO_PATH
            return MP_ENOENT;
        case 6:                     // FR_INVALID_NAME
        case 19:                    // FR_INVALID_PARAMETER
            return MP_EINVAL;
        case 7:                     // FR_DENIED
            return MP_EACCES;
        case 8:                     // FR_EXIST
            return MP_EEXIST;
        case 10:                    // FR_WRITE_PROTECTED
            return MP_EROFS;
        case 3:                     // FR_NOT_READY
        case 12:                    // FR_NOT_ENABLED
        case 13:                    // FR_NO_FILESYSTEM
            return MP_ENODEV;
        case 17:                    // FR_NOT_ENOUGH_CORE
            return MP_ENOMEM;
        case 18:                    // FR_TOO_MANY_OPEN_FILES
            return MP_EMFILE;
        default:
            return MP_EIO;
    }
}

static MP_NORETURN void raise_status(int r) {
    mp_raise_OSError(mp_vfs_cmm2_errno(r));
}

// "." and ".." taken out of a path from the drive's root ("/a/./b/../c" is "/a/c"), in place
static void normalise(char *p) {
    char *out = p;                  // p[0] is '/'
    const char *in = p;
    while (*in) {
        while (*in == '/') {
            in++;
        }
        const char *seg = in;
        while (*in && *in != '/') {
            in++;
        }
        size_t n = in - seg;
        if (n == 0 || (n == 1 && seg[0] == '.')) {
            continue;
        }
        if (n == 2 && seg[0] == '.' && seg[1] == '.') {
            while (out > p && *--out != '/') {
            }
            continue;
        }
        *out++ = '/';
        memmove(out, seg, n);
        out += n;
    }
    if (out == p) {
        *out++ = '/';
    }
    *out = 0;
}

// the path the loader is given: the drive, then the path from its root
static const char *vfs_cmm2_path(mp_obj_vfs_cmm2_t *self, mp_obj_t path_in, char *buf) {
    const char *path = mp_obj_str_get_str(path_in);
    size_t len = strlen(path);
    char *p = buf + 2;
    memcpy(buf, self->drive, 2);
    if (path[0] == '/') {
        if (len + 3 > PATH_MAX_CMM2) {
            mp_raise_OSError(MP_EINVAL);    // (too long: MicroPython has no ENAMETOOLONG)
        }
        memcpy(p, path, len + 1);
    } else {
        size_t c = strlen(self->cwd);
        if (c + len + 4 > PATH_MAX_CMM2) {
            mp_raise_OSError(MP_EINVAL);
        }
        memcpy(p, self->cwd, c);
        p[c] = '/';
        memcpy(p + c + 1, path, len + 1);
    }
    normalise(p);
    return buf;
}

mp_obj_t mp_vfs_cmm2_new(const char *drive) {
    mp_obj_vfs_cmm2_t *vfs = mp_obj_malloc(mp_obj_vfs_cmm2_t, &mp_type_vfs_cmm2);
    vfs->drive[0] = drive[0];
    vfs->drive[1] = ':';
    vfs->drive[2] = 0;
    strcpy(vfs->cwd, "/");
    vfs->readonly = false;
    return MP_OBJ_FROM_PTR(vfs);
}

static mp_obj_t vfs_cmm2_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    const char *drive = n_args ? mp_obj_str_get_str(args[0]) : "A:";
    char d = drive[0] & ~0x20;
    if ((d != 'A' && d != 'B' && d != 'C') || (drive[1] != ':' && drive[1] != 0)) {
        mp_raise_ValueError(MP_ERROR_TEXT("drive must be A:, B: or C:"));
    }
    char name[3] = { d, ':', 0 };
    return mp_vfs_cmm2_new(name);
}

static mp_import_stat_t mp_vfs_cmm2_import_stat(void *self_in, const char *path) {
    mp_obj_vfs_cmm2_t *self = self_in;
    char buf[PATH_MAX_CMM2];
    cmm2_stat st;
    vfs_cmm2_path(self, mp_obj_new_str_from_cstr(path), buf);
    if (cmm2->file_stat(buf, &st) != 0) {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    return st.is_dir ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_FILE;
}

static mp_obj_t vfs_cmm2_mount(mp_obj_t self_in, mp_obj_t readonly, mp_obj_t mkfs) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    if (mp_obj_is_true(readonly)) {
        self->readonly = true;
    }
    if (mp_obj_is_true(mkfs)) {
        mp_raise_OSError(MP_EPERM);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(vfs_cmm2_mount_obj, vfs_cmm2_mount);

static mp_obj_t vfs_cmm2_umount(mp_obj_t self_in) {
    (void)self_in;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(vfs_cmm2_umount_obj, vfs_cmm2_umount);

static void vfs_cmm2_require_writable(mp_obj_vfs_cmm2_t *self) {
    if (self->readonly) {
        mp_raise_OSError(MP_EROFS);
    }
}

static mp_obj_t vfs_cmm2_open(mp_obj_t self_in, mp_obj_t path_in, mp_obj_t mode_in) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    const char *mode = mp_obj_str_get_str(mode_in);
    if (self->readonly && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+') || strchr(mode, 'x'))) {
        mp_raise_OSError(MP_EROFS);
    }
    char buf[PATH_MAX_CMM2];
    return mp_vfs_cmm2_file_open(&mp_type_vfs_cmm2_textio, vfs_cmm2_path(self, path_in, buf), mode_in);
}
static MP_DEFINE_CONST_FUN_OBJ_3(vfs_cmm2_open_obj, vfs_cmm2_open);

static mp_obj_t vfs_cmm2_chdir(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    char buf[PATH_MAX_CMM2];
    cmm2_stat st;
    vfs_cmm2_path(self, path_in, buf);
    int r = cmm2->file_stat(buf, &st);
    if (r < 0) {
        raise_status(r);
    }
    if (!st.is_dir) {
        mp_raise_OSError(MP_ENOTDIR);
    }
    strcpy(self->cwd, buf + 2);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(vfs_cmm2_chdir_obj, vfs_cmm2_chdir);

static mp_obj_t vfs_cmm2_getcwd(mp_obj_t self_in) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_str_from_cstr(self->cwd);
}
static MP_DEFINE_CONST_FUN_OBJ_1(vfs_cmm2_getcwd_obj, vfs_cmm2_getcwd);

typedef struct _vfs_cmm2_ilistdir_it_t {
    mp_obj_base_t base;
    mp_fun_1_t iternext;
    mp_fun_1_t finaliser;
    bool is_str;
    int dir;                        // the loader's handle, or -1 once ended
} vfs_cmm2_ilistdir_it_t;

static mp_obj_t vfs_cmm2_ilistdir_it_iternext(mp_obj_t self_in) {
    vfs_cmm2_ilistdir_it_t *self = MP_OBJ_TO_PTR(self_in);
    static cmm2_dirent e;           // (264 bytes: not on the stack)
    if (self->dir < 0) {
        return MP_OBJ_STOP_ITERATION;
    }
    for (;;) {
        if (cmm2->dir_read(self->dir, &e) != 1) {
            cmm2->dir_close(self->dir);
            self->dir = -1;
            return MP_OBJ_STOP_ITERATION;
        }
        const char *fn = e.name;
        if (fn[0] == '.' && (fn[1] == 0 || (fn[1] == '.' && fn[2] == 0))) {
            continue;
        }
        // (name, type, inode, size), as FAT's VFS gives
        mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(4, NULL));
        if (self->is_str) {
            t->items[0] = mp_obj_new_str_from_cstr(fn);
        } else {
            t->items[0] = mp_obj_new_bytes((const byte *)fn, strlen(fn));
        }
        t->items[1] = MP_OBJ_NEW_SMALL_INT(e.is_dir ? MP_S_IFDIR : MP_S_IFREG);
        t->items[2] = MP_OBJ_NEW_SMALL_INT(0);
        t->items[3] = mp_obj_new_int_from_uint(e.size);
        return MP_OBJ_FROM_PTR(t);
    }
}

static mp_obj_t vfs_cmm2_ilistdir_it_del(mp_obj_t self_in) {
    vfs_cmm2_ilistdir_it_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->dir >= 0) {
        cmm2->dir_close(self->dir);
        self->dir = -1;
    }
    return mp_const_none;
}

static mp_obj_t vfs_cmm2_ilistdir(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    char buf[PATH_MAX_CMM2];
    vfs_cmm2_ilistdir_it_t *iter = mp_obj_malloc_with_finaliser(vfs_cmm2_ilistdir_it_t, &mp_type_polymorph_iter_with_finaliser);
    iter->iternext = vfs_cmm2_ilistdir_it_iternext;
    iter->finaliser = vfs_cmm2_ilistdir_it_del;
    iter->is_str = mp_obj_get_type(path_in) == &mp_type_str;
    iter->dir = -1;
    int h = cmm2->dir_open(vfs_cmm2_path(self, path_in, buf));
    if (h < 0) {
        raise_status(h);
    }
    iter->dir = h;
    return MP_OBJ_FROM_PTR(iter);
}
static MP_DEFINE_CONST_FUN_OBJ_2(vfs_cmm2_ilistdir_obj, vfs_cmm2_ilistdir);

static mp_obj_t vfs_cmm2_mkdir(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    char buf[PATH_MAX_CMM2];
    vfs_cmm2_require_writable(self);
    int r = cmm2->dir_make(vfs_cmm2_path(self, path_in, buf));
    if (r < 0) {
        raise_status(r);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(vfs_cmm2_mkdir_obj, vfs_cmm2_mkdir);

// a file (remove) or an empty directory (rmdir): FatFs's f_unlink takes either, so the kind is checked first
static mp_obj_t vfs_cmm2_unlink(mp_obj_t self_in, mp_obj_t path_in, bool dir) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    char buf[PATH_MAX_CMM2];
    cmm2_stat st;
    vfs_cmm2_require_writable(self);
    vfs_cmm2_path(self, path_in, buf);
    int r = cmm2->file_stat(buf, &st);
    if (r < 0) {
        raise_status(r);
    }
    if (st.is_dir != dir) {
        mp_raise_OSError(dir ? MP_ENOTDIR : MP_EISDIR);
    }
    if ((r = cmm2->file_remove(buf)) < 0) {
        raise_status(r);
    }
    return mp_const_none;
}

static mp_obj_t vfs_cmm2_remove(mp_obj_t self_in, mp_obj_t path_in) {
    return vfs_cmm2_unlink(self_in, path_in, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(vfs_cmm2_remove_obj, vfs_cmm2_remove);

static mp_obj_t vfs_cmm2_rmdir(mp_obj_t self_in, mp_obj_t path_in) {
    return vfs_cmm2_unlink(self_in, path_in, true);
}
static MP_DEFINE_CONST_FUN_OBJ_2(vfs_cmm2_rmdir_obj, vfs_cmm2_rmdir);

static mp_obj_t vfs_cmm2_rename(mp_obj_t self_in, mp_obj_t old_path_in, mp_obj_t new_path_in) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    char from[PATH_MAX_CMM2], to[PATH_MAX_CMM2];
    vfs_cmm2_require_writable(self);
    vfs_cmm2_path(self, old_path_in, from);
    vfs_cmm2_path(self, new_path_in, to);
    int r = cmm2->file_rename(from, to);
    if (r == -8) {                  // FR_EXIST: replaced, as POSIX has it (and FAT's VFS)
        cmm2->file_remove(to);
        r = cmm2->file_rename(from, to);
    }
    if (r < 0) {
        raise_status(r);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(vfs_cmm2_rename_obj, vfs_cmm2_rename);

static mp_obj_t vfs_cmm2_stat(mp_obj_t self_in, mp_obj_t path_in) {
    mp_obj_vfs_cmm2_t *self = MP_OBJ_TO_PTR(self_in);
    char buf[PATH_MAX_CMM2];
    cmm2_stat st;
    int r = cmm2->file_stat(vfs_cmm2_path(self, path_in, buf), &st);
    if (r < 0) {
        raise_status(r);
    }
    mp_uint_t mtime = 0;
    if (st.year) {
        mtime = timeutils_seconds_since_epoch(st.year, st.month, st.day, st.hour, st.minute, st.second);
    }
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    t->items[0] = MP_OBJ_NEW_SMALL_INT(st.is_dir ? MP_S_IFDIR : MP_S_IFREG);
    t->items[1] = MP_OBJ_NEW_SMALL_INT(0);
    t->items[2] = MP_OBJ_NEW_SMALL_INT(0);
    t->items[3] = MP_OBJ_NEW_SMALL_INT(0);
    t->items[4] = MP_OBJ_NEW_SMALL_INT(0);
    t->items[5] = MP_OBJ_NEW_SMALL_INT(0);
    t->items[6] = mp_obj_new_int_from_uint(st.size);
    t->items[7] = mp_obj_new_int_from_uint(mtime);
    t->items[8] = mp_obj_new_int_from_uint(mtime);
    t->items[9] = mp_obj_new_int_from_uint(mtime);
    return MP_OBJ_FROM_PTR(t);
}
static MP_DEFINE_CONST_FUN_OBJ_2(vfs_cmm2_stat_obj, vfs_cmm2_stat);

static const mp_rom_map_elem_t vfs_cmm2_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&vfs_cmm2_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_umount), MP_ROM_PTR(&vfs_cmm2_umount_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&vfs_cmm2_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&vfs_cmm2_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&vfs_cmm2_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_ilistdir), MP_ROM_PTR(&vfs_cmm2_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&vfs_cmm2_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&vfs_cmm2_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&vfs_cmm2_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&vfs_cmm2_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&vfs_cmm2_stat_obj) },
};
static MP_DEFINE_CONST_DICT(vfs_cmm2_locals_dict, vfs_cmm2_locals_dict_table);

static const mp_vfs_proto_t vfs_cmm2_proto = {
    .import_stat = mp_vfs_cmm2_import_stat,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_vfs_cmm2,
    MP_QSTR_VfsCMM2,
    MP_TYPE_FLAG_NONE,
    make_new, vfs_cmm2_make_new,
    protocol, &vfs_cmm2_proto,
    locals_dict, &vfs_cmm2_locals_dict
    );
