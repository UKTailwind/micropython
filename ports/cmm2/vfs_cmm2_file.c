/*
 * MicroPython for the Colour Maximite 2 G2: the files of the VFS on the loader's file services (vfs_cmm2.c). Modelled
 * on extmod/vfs_posix_file.c (Damien P. George), with the loader's file_read, file_write, file_seek... in place of
 * POSIX's. The loader keeps eight files open at once.
 *
 * The MIT License (MIT), as MicroPython. Copyright (c) 2013-2018 Damien P. George; 2026 the CMM2 port.
 */
#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "cmm2.h"
#include "vfs_cmm2.h"

typedef struct _mp_obj_vfs_cmm2_file_t {
    mp_obj_base_t base;
    int h;                          // the loader's handle, or -1 when closed
} mp_obj_vfs_cmm2_file_t;

static void check_open(const mp_obj_vfs_cmm2_file_t *o) {
    if (o->h < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("I/O operation on closed file"));
    }
}

static void vfs_cmm2_file_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_vfs_cmm2_file_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<io.%s %d>", mp_obj_get_type_str(self_in), self->h);
}

mp_obj_t mp_vfs_cmm2_file_open(const mp_obj_type_t *type, const char *path, mp_obj_t mode_in) {
    const char *mode_s = mp_obj_str_get_str(mode_in);
    int mode = 0;
    bool exclusive = false;
    while (*mode_s) {
        switch (*mode_s++) {
            case 'r':
                mode |= CMM2_O_READ;
                break;
            case 'w':
                mode |= CMM2_O_WRITE | CMM2_O_CREATE;
                break;
            case 'x':
                mode |= CMM2_O_WRITE | CMM2_O_CREATE;
                exclusive = true;
                break;
            case 'a':
                mode |= CMM2_O_WRITE | CMM2_O_APPEND;
                break;
            case '+':
                mode |= CMM2_O_READ | CMM2_O_WRITE;
                break;
            case 'b':
                type = &mp_type_vfs_cmm2_fileio;
                break;
            case 't':
                type = &mp_type_vfs_cmm2_textio;
                break;
        }
    }
    if (mode == 0) {
        mode = CMM2_O_READ;
    }
    if (exclusive) {
        cmm2_stat st;
        if (cmm2->file_stat(path, &st) == 0) {
            mp_raise_OSError(MP_EEXIST);
        }
    }
    mp_obj_vfs_cmm2_file_t *o = mp_obj_malloc_with_finaliser(mp_obj_vfs_cmm2_file_t, type);
    o->h = -1;
    int h = cmm2->file_open(path, mode);
    if (h < 0) {
        mp_raise_OSError(mp_vfs_cmm2_errno(h));
    }
    o->h = h;
    return MP_OBJ_FROM_PTR(o);
}

static mp_uint_t vfs_cmm2_file_read(mp_obj_t o_in, void *buf, mp_uint_t size, int *errcode) {
    mp_obj_vfs_cmm2_file_t *o = MP_OBJ_TO_PTR(o_in);
    check_open(o);
    int32_t r = cmm2->file_read(o->h, buf, size);
    if (r < 0) {
        *errcode = mp_vfs_cmm2_errno(r);
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)r;
}

static mp_uint_t vfs_cmm2_file_write(mp_obj_t o_in, const void *buf, mp_uint_t size, int *errcode) {
    mp_obj_vfs_cmm2_file_t *o = MP_OBJ_TO_PTR(o_in);
    check_open(o);
    int32_t r = cmm2->file_write(o->h, buf, size);
    if (r < 0) {
        *errcode = mp_vfs_cmm2_errno(r);
        return MP_STREAM_ERROR;
    }
    if ((mp_uint_t)r != size) {     // (a full card: FatFs writes what fits)
        *errcode = MP_ENOSPC;
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)r;
}

static mp_uint_t vfs_cmm2_file_ioctl(mp_obj_t o_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    mp_obj_vfs_cmm2_file_t *o = MP_OBJ_TO_PTR(o_in);
    switch (request) {
        case MP_STREAM_FLUSH: {
            check_open(o);
            int r = cmm2->file_sync(o->h);
            if (r < 0) {
                *errcode = mp_vfs_cmm2_errno(r);
                return MP_STREAM_ERROR;
            }
            return 0;
        }
        case MP_STREAM_SEEK: {
            check_open(o);
            struct mp_stream_seek_t *s = (struct mp_stream_seek_t *)arg;
            int64_t pos = s->offset;
            if (s->whence == MP_SEEK_CUR) {
                pos += cmm2->file_tell(o->h);
            } else if (s->whence == MP_SEEK_END) {
                pos += cmm2->file_size(o->h);
            }
            if (pos < 0) {
                *errcode = MP_EINVAL;
                return MP_STREAM_ERROR;
            }
            int r = cmm2->file_seek(o->h, (uint32_t)pos);
            if (r < 0) {
                *errcode = mp_vfs_cmm2_errno(r);
                return MP_STREAM_ERROR;
            }
            s->offset = cmm2->file_tell(o->h);
            return 0;
        }
        case MP_STREAM_CLOSE:
            if (o->h >= 0) {
                cmm2->file_close(o->h);
            }
            o->h = -1;
            return 0;
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}

static const mp_rom_map_elem_t vfs_cmm2_rawfile_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(vfs_cmm2_rawfile_locals_dict, vfs_cmm2_rawfile_locals_dict_table);

static const mp_stream_p_t vfs_cmm2_fileio_stream_p = {
    .read = vfs_cmm2_file_read,
    .write = vfs_cmm2_file_write,
    .ioctl = vfs_cmm2_file_ioctl,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_vfs_cmm2_fileio,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, vfs_cmm2_file_print,
    protocol, &vfs_cmm2_fileio_stream_p,
    locals_dict, &vfs_cmm2_rawfile_locals_dict
    );

static const mp_stream_p_t vfs_cmm2_textio_stream_p = {
    .read = vfs_cmm2_file_read,
    .write = vfs_cmm2_file_write,
    .ioctl = vfs_cmm2_file_ioctl,
    .is_text = true,
};

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_vfs_cmm2_textio,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, vfs_cmm2_file_print,
    protocol, &vfs_cmm2_textio_stream_p,
    locals_dict, &vfs_cmm2_rawfile_locals_dict
    );
