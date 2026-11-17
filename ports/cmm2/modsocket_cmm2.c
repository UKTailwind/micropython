/*
 * The socket module on the Colour Maximite 2: the firmware's network (V8 with the Wi-Fi module and OPTION WIFI), through
 * the loader's sock_ and net_ services (version 21, the firmware's Src/net/LoaderNet.c: lwIP's TCP and UDP as eight
 * handles). The same module as rp2's (extmod/modlwip.c) as programs see it:
 *
 *   socket.socket([af, type, proto]), getaddrinfo(host, port, ...), and on a socket: connect, bind, listen, accept,
 *   send, sendall, sendto, recv, recvfrom, read, readinto, readline, write, setblocking, settimeout, setsockopt (taken
 *   and ignored), makefile, close; the stream protocol, so tls.SSLContext.wrap_socket, select.poll and asyncio work on
 *   it. Addresses are (host, port), host a dotted address or a name (looked up).
 *
 * The firmware pumps the network only through net_poll while a program runs: the VM's hook calls it (mphalport.c) from
 * the first use of a socket or of the network module on (cmm2_net_used), and the waits here call it too. A socket's
 * handle is given back when it is closed, or by its finaliser when it is dropped (the firmware has eight).
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <string.h>
#include <stdio.h>
#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/gc.h"
#include "cmm2.h"
#include "modcmm2.h"

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define AF_INET         2
#define AF_INET6        10
#define IPPROTO_IP      0
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define SOL_SOCKET      1
#define SO_REUSEADDR    4
#define SO_BROADCAST    32
#define TCP_NODELAY     1

#define ST_CONNECTED    1                       // (sock_state's bits)
#define ST_DATA         2
#define ST_ROOM         4
#define ST_PEERCLOSED   8

bool cmm2_net_used;                             // (mphalport.c: net_poll from the VM's hook once true)

typedef struct _socket_obj_t {
    mp_obj_base_t base;
    int8_t h;                                   // the firmware's handle, -1 closed
    uint8_t type;                               // SOCK_STREAM or SOCK_DGRAM
    int32_t timeout_ms;                         // -1 blocking, 0 non-blocking
} socket_obj_t;

static const mp_obj_type_t socket_type;

// the network services there: version 21, and the Wi-Fi up (from the first use on, the VM's hook pumps it)
void cmm2_net_check(void) {
    if (cmm2->version < 21) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("the network needs a newer CMM2 firmware"));
    }
    cmm2_net_used = true;
}

static MP_NORETURN void raise_err(int r) {
    if (r == -MP_ENODEV) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no network: the Wi-Fi module and OPTION WIFI are needed"));
    }
    mp_raise_OSError(-r);
}

static void check_open(socket_obj_t *s) {
    if (s->h < 0) {
        mp_raise_OSError(MP_EBADF);
    }
}

// a short wait for the network: pumped, Ctrl-C and the scheduler seen to
static void net_wait(void) {
    cmm2->net_poll();
    mp_event_handle_nowait();
    cmm2->delay_us(200);
}

// the handle's state once one of the bits in want is set (or its error); -EAGAIN if non-blocking, -ETIMEDOUT
static int wait_state(socket_obj_t *s, int want) {
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {
        cmm2->net_poll();
        int st = cmm2->sock_state(s->h);
        if (st < 0 || (st & want)) {
            return st;
        }
        if (s->timeout_ms == 0) {
            return -MP_EAGAIN;
        }
        if (s->timeout_ms > 0 && (int32_t)(mp_hal_ticks_ms() - start) >= s->timeout_ms) {
            return -MP_ETIMEDOUT;
        }
        net_wait();
    }
}

// a name's (or a dotted address's) IPv4 address, looked up by the firmware's DNS (10 s at most)
static void resolve(const char *host, uint8_t ip[4]) {
    uint32_t start = mp_hal_ticks_ms();
    for (;;) {
        int r = cmm2->net_dns(host, ip);
        if (r == 0) {
            return;
        }
        if (r < 0) {
            raise_err(r);
        }
        if (mp_hal_ticks_ms() - start > 10000) {
            mp_raise_OSError(MP_ETIMEDOUT);
        }
        net_wait();
    }
}

// (host, port) -> its address and port
static int parse_addr(mp_obj_t addr, uint8_t ip[4]) {
    mp_obj_t *items;
    mp_obj_get_array_fixed_n(addr, 2, &items);
    const char *host = mp_obj_str_get_str(items[0]);
    if (*host == 0) {
        memset(ip, 0, 4);
    } else {
        resolve(host, ip);
    }
    return mp_obj_get_int(items[1]);
}

static mp_obj_t format_addr(const uint8_t ip[4], int port) {
    char s[16];
    snprintf(s, sizeof(s), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    mp_obj_t t[2] = { mp_obj_new_str_from_cstr(s), MP_OBJ_NEW_SMALL_INT(port) };
    return mp_obj_new_tuple(2, t);
}

static socket_obj_t *socket_new(int type, int h) {
    socket_obj_t *s = mp_obj_malloc_with_finaliser(socket_obj_t, &socket_type);
    s->h = h;
    s->type = type;
    s->timeout_ms = -1;
    return s;
}

// socket.socket(af=AF_INET, type=SOCK_STREAM, proto=0)
static mp_obj_t socket_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 4, false);
    cmm2_net_check();
    int af = n_args > 0 ? mp_obj_get_int(args[0]) : AF_INET;
    int t = n_args > 1 ? mp_obj_get_int(args[1]) : SOCK_STREAM;
    if (af != AF_INET || (t != SOCK_STREAM && t != SOCK_DGRAM)) {
        mp_raise_OSError(MP_EINVAL);
    }
    socket_obj_t *s = socket_new(t, -1);        // (made first: a full table is collected below)
    int h = cmm2->sock_open(t == SOCK_STREAM ? 1 : 2);
    if (h == -MP_EMFILE) {
        gc_collect();                           // (sockets dropped unclosed: their finalisers give the handles back)
        h = cmm2->sock_open(t == SOCK_STREAM ? 1 : 2);
    }
    if (h < 0) {
        raise_err(h);
    }
    s->h = h;
    return MP_OBJ_FROM_PTR(s);
}

static void socket_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<socket h=%d type=%d timeout=%d>", s->h, s->type, (int)s->timeout_ms);
}

static mp_obj_t socket_close(mp_obj_t self_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->h >= 0) {
        cmm2->sock_close(s->h);
        s->h = -1;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_close_obj, socket_close);

static mp_obj_t socket_bind(mp_obj_t self_in, mp_obj_t addr) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    uint8_t ip[4];
    check_open(s);
    int r = cmm2->sock_bind(s->h, parse_addr(addr, ip));
    if (r < 0) {
        raise_err(r);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_bind_obj, socket_bind);

static mp_obj_t socket_listen(size_t n_args, const mp_obj_t *args) {
    socket_obj_t *s = MP_OBJ_TO_PTR(args[0]);
    check_open(s);
    int r = cmm2->sock_listen(s->h, n_args > 1 ? mp_obj_get_int(args[1]) : 2);
    if (r < 0) {
        raise_err(r);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_listen_obj, 1, 2, socket_listen);

static mp_obj_t socket_accept(mp_obj_t self_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    check_open(s);
    int st = wait_state(s, ST_DATA);
    if (st < 0) {
        raise_err(st);
    }
    socket_obj_t *n = socket_new(SOCK_STREAM, -1);
    uint8_t ip[4];
    int port;
    int h = cmm2->sock_accept(s->h, ip, &port);
    if (h < 0) {
        raise_err(h);
    }
    n->h = h;
    mp_obj_t t[2] = { MP_OBJ_FROM_PTR(n), format_addr(ip, port) };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(socket_accept_obj, socket_accept);

static mp_obj_t socket_connect(mp_obj_t self_in, mp_obj_t addr) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    uint8_t ip[4];
    check_open(s);
    int port = parse_addr(addr, ip);
    int r = cmm2->sock_connect(s->h, ip, port);
    if (r < 0) {
        raise_err(r);
    }
    if (s->type == SOCK_STREAM) {
        if (s->timeout_ms == 0) {
            mp_raise_OSError(MP_EINPROGRESS);   // (as rp2's: select or poll says when it is made)
        }
        int st = wait_state(s, ST_CONNECTED | ST_PEERCLOSED);
        if (st < 0) {
            raise_err(st);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_connect_obj, socket_connect);

// up to len bytes sent (TCP: waiting for room as the timeout allows), or a negative errno
static mp_int_t sock_send_some(socket_obj_t *s, const uint8_t *buf, mp_uint_t len, const uint8_t *ip, int port) {
    if (s->type == SOCK_DGRAM) {
        return cmm2->sock_send(s->h, buf, len, ip, port);
    }
    for (;;) {
        int n = cmm2->sock_send(s->h, buf, len, NULL, 0);
        if (n != 0 || len == 0) {
            return n;
        }
        int st = wait_state(s, ST_ROOM);
        if (st < 0) {
            return st;
        }
    }
}

// up to len bytes received (waiting as the timeout allows): 0 the peer's end (TCP), or a negative errno
static mp_int_t sock_recv_some(socket_obj_t *s, uint8_t *buf, mp_uint_t len, uint8_t *ip, int *port) {
    for (;;) {
        int n = cmm2->sock_recv(s->h, buf, len, ip, port);
        if (n != -MP_EAGAIN) {
            return n;
        }
        int st = wait_state(s, ST_DATA | ST_PEERCLOSED);
        if (st < 0) {
            return st;
        }
    }
}

static mp_obj_t socket_send(mp_obj_t self_in, mp_obj_t buf_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bi;
    check_open(s);
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_READ);
    mp_int_t n = sock_send_some(s, bi.buf, bi.len, NULL, 0);
    if (n < 0) {
        raise_err(n);
    }
    return MP_OBJ_NEW_SMALL_INT(n);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_send_obj, socket_send);

static mp_obj_t socket_sendall(mp_obj_t self_in, mp_obj_t buf_in) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bi;
    check_open(s);
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_READ);
    const uint8_t *p = bi.buf;
    mp_uint_t left = bi.len;
    while (left) {
        mp_int_t n = sock_send_some(s, p, left, NULL, 0);
        if (n < 0) {
            raise_err(n);
        }
        p += n;
        left -= n;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_sendall_obj, socket_sendall);

static mp_obj_t socket_sendto(mp_obj_t self_in, mp_obj_t buf_in, mp_obj_t addr) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bi;
    uint8_t ip[4];
    check_open(s);
    mp_get_buffer_raise(buf_in, &bi, MP_BUFFER_READ);
    int port = parse_addr(addr, ip);
    mp_int_t n = sock_send_some(s, bi.buf, bi.len, ip, port);
    if (n < 0) {
        raise_err(n);
    }
    return MP_OBJ_NEW_SMALL_INT(n);
}
static MP_DEFINE_CONST_FUN_OBJ_3(socket_sendto_obj, socket_sendto);

static mp_obj_t socket_recv_common(mp_obj_t self_in, mp_obj_t len_in, bool from) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    check_open(s);
    mp_int_t len = mp_obj_get_int(len_in);
    vstr_t vstr;
    vstr_init_len(&vstr, len);
    uint8_t ip[4] = {0, 0, 0, 0};
    int port = 0;
    mp_int_t n = sock_recv_some(s, (uint8_t *)vstr.buf, len, ip, &port);
    if (n < 0) {
        vstr_clear(&vstr);
        raise_err(n);
    }
    vstr.len = n;
    mp_obj_t data = mp_obj_new_bytes_from_vstr(&vstr);
    if (!from) {
        return data;
    }
    mp_obj_t t[2] = { data, format_addr(ip, port) };
    return mp_obj_new_tuple(2, t);
}

static mp_obj_t socket_recv(mp_obj_t self_in, mp_obj_t len_in) {
    return socket_recv_common(self_in, len_in, false);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_recv_obj, socket_recv);

static mp_obj_t socket_recvfrom(mp_obj_t self_in, mp_obj_t len_in) {
    return socket_recv_common(self_in, len_in, true);
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_recvfrom_obj, socket_recvfrom);

static mp_obj_t socket_setsockopt(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;                                 // (SO_REUSEADDR and the like: lwIP's defaults serve)
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_setsockopt_obj, 4, 4, socket_setsockopt);

static mp_obj_t socket_settimeout(mp_obj_t self_in, mp_obj_t t) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (t == mp_const_none) {
        s->timeout_ms = -1;
    } else {
        #if MICROPY_PY_BUILTINS_FLOAT
        mp_float_t f = mp_obj_get_float(t);
        s->timeout_ms = f <= 0 ? 0 : (int32_t)(f * 1000);
        #else
        s->timeout_ms = mp_obj_get_int(t) * 1000;
        #endif
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_settimeout_obj, socket_settimeout);

static mp_obj_t socket_setblocking(mp_obj_t self_in, mp_obj_t flag) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    s->timeout_ms = mp_obj_is_true(flag) ? -1 : 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(socket_setblocking_obj, socket_setblocking);

static mp_obj_t socket_makefile(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(socket_makefile_obj, 1, 3, socket_makefile);

// the stream protocol (read, write, readline, tls's wrap_socket, select and asyncio)
static mp_uint_t socket_stream_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->h < 0) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    mp_int_t n = sock_recv_some(s, buf, size, NULL, NULL);
    if (n < 0) {
        *errcode = n == -MP_ETIMEDOUT ? MP_ETIMEDOUT : -n;
        return MP_STREAM_ERROR;
    }
    return n;
}

static mp_uint_t socket_stream_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (s->h < 0) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    mp_int_t n = sock_send_some(s, buf, size, NULL, 0);
    if (n < 0) {
        *errcode = -n;
        return MP_STREAM_ERROR;
    }
    if (n == 0 && size > 0) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }
    return n;
}

static mp_uint_t socket_stream_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    socket_obj_t *s = MP_OBJ_TO_PTR(self_in);
    if (request == MP_STREAM_CLOSE) {
        socket_close(self_in);
        return 0;
    }
    if (request == MP_STREAM_POLL) {
        if (s->h < 0) {
            return MP_STREAM_POLL_NVAL;
        }
        cmm2->net_poll();
        int st = cmm2->sock_state(s->h);
        mp_uint_t ret = 0;
        if (st < 0) {
            ret = MP_STREAM_POLL_ERR | MP_STREAM_POLL_HUP;
            ret |= arg & (MP_STREAM_POLL_RD | MP_STREAM_POLL_WR);
        } else {
            if ((arg & MP_STREAM_POLL_RD) && (st & (ST_DATA | ST_PEERCLOSED))) {
                ret |= MP_STREAM_POLL_RD;
            }
            if ((arg & MP_STREAM_POLL_WR) && (st & ST_ROOM)) {
                ret |= MP_STREAM_POLL_WR;
            }
            if (st & ST_PEERCLOSED) {
                ret |= MP_STREAM_POLL_HUP;
            }
        }
        return ret;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

static const mp_rom_map_elem_t socket_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&socket_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&socket_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind), MP_ROM_PTR(&socket_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen), MP_ROM_PTR(&socket_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_accept), MP_ROM_PTR(&socket_accept_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&socket_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_send), MP_ROM_PTR(&socket_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendall), MP_ROM_PTR(&socket_sendall_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendto), MP_ROM_PTR(&socket_sendto_obj) },
    { MP_ROM_QSTR(MP_QSTR_recv), MP_ROM_PTR(&socket_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_recvfrom), MP_ROM_PTR(&socket_recvfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_setsockopt), MP_ROM_PTR(&socket_setsockopt_obj) },
    { MP_ROM_QSTR(MP_QSTR_settimeout), MP_ROM_PTR(&socket_settimeout_obj) },
    { MP_ROM_QSTR(MP_QSTR_setblocking), MP_ROM_PTR(&socket_setblocking_obj) },
    { MP_ROM_QSTR(MP_QSTR_makefile), MP_ROM_PTR(&socket_makefile_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
};
static MP_DEFINE_CONST_DICT(socket_locals_dict, socket_locals_dict_table);

static const mp_stream_p_t socket_stream_p = {
    .read = socket_stream_read,
    .write = socket_stream_write,
    .ioctl = socket_stream_ioctl,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    socket_type,
    MP_QSTR_socket,
    MP_TYPE_FLAG_NONE,
    make_new, socket_make_new,
    print, socket_print,
    protocol, &socket_stream_p,
    locals_dict, &socket_locals_dict
    );

// getaddrinfo(host, port, af=0, type=0, proto=0, flags=0): [(AF_INET, type, proto, '', (address, port))]
static mp_obj_t mod_socket_getaddrinfo(size_t n_args, const mp_obj_t *args) {
    cmm2_net_check();
    uint8_t ip[4];
    resolve(mp_obj_str_get_str(args[0]), ip);
    int type = n_args > 3 ? mp_obj_get_int(args[3]) : 0;
    int proto = n_args > 4 ? mp_obj_get_int(args[4]) : 0;
    mp_obj_t t[5] = {
        MP_OBJ_NEW_SMALL_INT(AF_INET),
        MP_OBJ_NEW_SMALL_INT(type ? type : SOCK_STREAM),
        MP_OBJ_NEW_SMALL_INT(proto),
        MP_OBJ_NEW_QSTR(MP_QSTR_),
        format_addr(ip, mp_obj_get_int(args[1])),
    };
    mp_obj_t item = mp_obj_new_tuple(5, t);
    return mp_obj_new_list(1, &item);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_socket_getaddrinfo_obj, 2, 6, mod_socket_getaddrinfo);

static const mp_rom_map_elem_t mp_module_socket_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_socket) },
    { MP_ROM_QSTR(MP_QSTR_socket), MP_ROM_PTR(&socket_type) },
    { MP_ROM_QSTR(MP_QSTR_getaddrinfo), MP_ROM_PTR(&mod_socket_getaddrinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET), MP_ROM_INT(AF_INET) },
    { MP_ROM_QSTR(MP_QSTR_AF_INET6), MP_ROM_INT(AF_INET6) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_STREAM), MP_ROM_INT(SOCK_STREAM) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_DGRAM), MP_ROM_INT(SOCK_DGRAM) },
    { MP_ROM_QSTR(MP_QSTR_SOCK_RAW), MP_ROM_INT(SOCK_RAW) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_IP), MP_ROM_INT(IPPROTO_IP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_TCP), MP_ROM_INT(IPPROTO_TCP) },
    { MP_ROM_QSTR(MP_QSTR_IPPROTO_UDP), MP_ROM_INT(IPPROTO_UDP) },
    { MP_ROM_QSTR(MP_QSTR_SOL_SOCKET), MP_ROM_INT(SOL_SOCKET) },
    { MP_ROM_QSTR(MP_QSTR_SO_REUSEADDR), MP_ROM_INT(SO_REUSEADDR) },
    { MP_ROM_QSTR(MP_QSTR_SO_BROADCAST), MP_ROM_INT(SO_BROADCAST) },
    { MP_ROM_QSTR(MP_QSTR_TCP_NODELAY), MP_ROM_INT(TCP_NODELAY) },
};
static MP_DEFINE_CONST_DICT(mp_module_socket_globals, mp_module_socket_globals_table);

const mp_obj_module_t mp_module_socket = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_socket_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_socket, mp_module_socket);
