/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND...
 */

// The `draw3d` module: MMBasic's DRAW3D 3D engine (PicoMite graphics/
// Draw3D.c, by Geoff Graham and Peter Mather), ported to a MicroPython
// module. Objects are polyhedra: vertices stored as unit-vector+magnitude
// quaternions, rotated with MMBasic's 5-element rotation quaternions,
// backface-culled by the camera-ray dot product, painter-depth-sorted and
// drawn (edges + fills) on the current hdmi write target through the shared
// drawing helpers. Colours are RGB888, converted per mode at draw time.
//
//   draw3d.camera(c, viewplane, x=0, y=0, panx=0, pany=0)
//   draw3d.create(n, nv, nf, cam, vertices, facecount, faces, colours,
//                 edge=None, fill=None)   # edge/fill = per-face colour INDEX
//   draw3d.show(n, x, y, z, nonormals=0, depthmode=0)
//   draw3d.write(n, x, y, z, ...)         # as show, without the erase
//   draw3d.rotate(q, n, ...)              # q = (w, x, y, z, m); from original
//   draw3d.reset(n, ...)                  # rotated becomes the new original
//   draw3d.hide(n, ...) / hide_all() / restore(n, ...)
//   draw3d.close(n, ...) / close_all()
//   draw3d.set_flags(n, flag, face, nbr)  # 1 hide, 2 red, 4 invert, 8 light
//   draw3d.light(n, x, y, z, ambient)     # ambient 0..100
//   draw3d.q_create(theta, x, y, z)       # MMBasic MATH Q_CREATE -> 5-tuple
//   draw3d.query(n, "xmin"/"ymax"/"x"/"z"/"distance"/...)
//
// depthmode: 0 = centroid depth sort (MMBasic default), 1 = max-vertex
// depth sort. (MMBasic's depthmode 2, z-buffer hidden-line, is not ported
// yet.) Face flags and per-face lighting behave as MMBasic's.

#include <math.h>
#include <string.h>

#include "py/runtime.h"

#if MICROPY_HW_ENABLE_HDMI

#include "hdmi_priv.h"

#define MAX3D  8 // MMBasic configuration.h
#define MAXCAM 3
// Single precision throughout, exactly as MMBasic (configuration.h: FLOAT3D
// = float, sqrt3d = sqrtf...): the RP2350's FPU is single-precision only,
// so doubles here would fall back to (slow) software arithmetic.
#define D3D_FLOAT float
#define sqrt3d  sqrtf
#define round3d roundf
#define fabs3d  fabsf

typedef struct {
    D3D_FLOAT w, x, y, z, m;
} d3d_quat_t;

typedef struct {
    D3D_FLOAT x, y, z;
} d3d_vec_t;

typedef struct {
    D3D_FLOAT x, y, z, viewplane, panx, pany;
} d3d_cam_t;

typedef struct {
    d3d_quat_t *q_vertices;
    d3d_quat_t *r_vertices;
    d3d_quat_t *q_centroids;
    d3d_quat_t *r_centroids;
    d3d_vec_t *normals;
    uint8_t *facecount;
    uint16_t *facestart;
    uint32_t *fill;   // RGB888; 0xFFFFFFFF = outline only
    uint32_t *line;   // RGB888
    uint16_t *face_x_vert;
    uint8_t *flags;
    D3D_FLOAT *dots;
    D3D_FLOAT *depth;
    int *depthindex;
    d3d_vec_t light;
    d3d_vec_t current;
    D3D_FLOAT distance;
    D3D_FLOAT ambient;
    int tot_face_x_vert;
    int xmin, xmax, ymin, ymax;
    int nv, nf, vmax, cam;
    int nonormals, depthmode;
} d3d_obj_t;

// One m_malloc block per object slot, rooted so the GC keeps it.
MP_REGISTER_ROOT_POINTER(uint8_t *draw3d_slots[9]);
static d3d_cam_t d3d_cams[MAXCAM + 1];
static bool d3d_cams_init = false;

static void d3d_cams_ensure(void) {
    if (!d3d_cams_init) {
        d3d_cams_init = true;
        for (int i = 0; i <= MAXCAM; i++) {
            d3d_cams[i].viewplane = -32767;
        }
    }
}

static d3d_obj_t *d3d_get(int n, bool required) {
    if (n < 1 || n > MAX3D) {
        mp_raise_ValueError(MP_ERROR_TEXT("object number must be 1..8"));
    }
    d3d_obj_t *o = (d3d_obj_t *)MP_STATE_PORT(draw3d_slots)[n];
    if (o == NULL && required) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("3D object %d does not exist"), n);
    }
    return o;
}

// --- quaternion helpers (MMBasic T_Mult / T_Invert / q_rotate, verbatim) ---
static void t_mult(const D3D_FLOAT *q1, const D3D_FLOAT *q2, D3D_FLOAT *n) {
    D3D_FLOAT a1 = q1[0], a2 = q2[0], b1 = q1[1], b2 = q2[1];
    D3D_FLOAT c1 = q1[2], c2 = q2[2], d1 = q1[3], d2 = q2[3];
    n[0] = a1 * a2 - b1 * b2 - c1 * c2 - d1 * d2;
    n[1] = a1 * b2 + b1 * a2 + c1 * d2 - d1 * c2;
    n[2] = a1 * c2 - b1 * d2 + c1 * a2 + d1 * b2;
    n[3] = a1 * d2 + b1 * c2 - c1 * b2 + d1 * a2;
    n[4] = q1[4] * q2[4];
}

static void q_rotate(const d3d_quat_t *in, const d3d_quat_t *rot, d3d_quat_t *out) {
    d3d_quat_t temp, qinv;
    t_mult((const D3D_FLOAT *)rot, (const D3D_FLOAT *)in, (D3D_FLOAT *)&temp);
    qinv.w = rot->w;
    qinv.x = -rot->x;
    qinv.y = -rot->y;
    qinv.z = -rot->z;
    qinv.m = rot->m;
    t_mult((const D3D_FLOAT *)&temp, (const D3D_FLOAT *)&qinv, (D3D_FLOAT *)out);
}

static void d3d_normalise(d3d_vec_t *v) {
    D3D_FLOAT n = sqrt3d(v->x * v->x + v->y * v->y + v->z * v->z);
    v->x /= n;
    v->y /= n;
    v->z /= n;
}

// MMBasic depthsort: descending bubble sort carrying the index array.
static void depthsort(D3D_FLOAT *farray, int n, int *index) {
    int j = n, s = 1;
    while (s) {
        s = 0;
        for (int i = 1; i < j; i++) {
            if (farray[i] > farray[i - 1]) {
                D3D_FLOAT f = farray[i];
                farray[i] = farray[i - 1];
                farray[i - 1] = f;
                s = 1;
                int t = index[i - 1];
                index[i - 1] = index[i];
                index[i] = t;
            }
        }
        j--;
    }
}

// MMBasic DrawPolygon: outline-only when fill == 0xFFFFFFFF, else fill the
// face and outline it when the colours differ. (All faces render through
// the shared scanline fill; MMBasic uses DrawTriangle for 3/4-vertex faces,
// which rasterises identically for convex faces.)
static void d3d_draw_polygon(d3d_obj_t *o, const short *xc, const short *yc, int face) {
    int fc = o->facecount[face];
    int32_t c = hdmi_colour_native(o->line[face]);
    int16_t pts[2 * 32];
    for (int i = 0; i < fc && i < 32; i++) {
        pts[i * 2] = xc[i];
        pts[i * 2 + 1] = yc[i];
    }
    if (o->fill[face] == 0xFFFFFFFF) {
        for (int i = 0; i < fc; i++) {
            int j = (i + 1 == fc) ? 0 : i + 1;
            hdmi_draw_line_raw(xc[i], yc[i], xc[j], yc[j], c);
        }
    } else {
        int32_t f = hdmi_colour_native(o->fill[face]);
        hdmi_polyfill_raw(pts, fc, f, 0);
        if (f != c) {
            for (int i = 0; i < fc; i++) {
                int j = (i + 1 == fc) ? 0 : i + 1;
                hdmi_draw_line_raw(xc[i], yc[i], xc[j], yc[j], c);
            }
        }
    }
}

// --- display3d (MMBasic, verbatim shape; depthmode 2 not ported) ------------
static void display3d(int n, D3D_FLOAT x, D3D_FLOAT y, D3D_FLOAT z,
    int clear, int nonormals, int depthmode) {
    d3d_obj_t *o = d3d_get(n, true);
    d3d_cam_t *cam = &d3d_cams[o->cam];
    d3d_vec_t ray, lighting = {0}, p1, p2, p3, U, V;
    D3D_FLOAT x1, y1, z1, tmp, at, bt, ct, t;
    D3D_FLOAT C = 1, D = -cam->viewplane;
    int maxW = hdmi_w, maxH = hdmi_h;
    short xcoord[32], ycoord[32];
    int csave = 0, fsave = 0;

    if (o->xmin != 32767 && clear) {
        hdmi_fill_rect_raw(o->xmin, o->ymin, o->xmax, o->ymax, 0);
    }
    o->xmin = 32767;
    o->ymin = 32767;
    o->xmax = -32767;
    o->ymax = -32767;
    o->distance = 0.0;

    for (int f = 0; f < o->nf; f++) {
        int vp = o->facestart[f];
        #define RV(i) (o->r_vertices[o->face_x_vert[(i)]])
        p1.x = RV(vp + 1).x * RV(vp + 1).m + x;
        p1.y = RV(vp + 1).y * RV(vp + 1).m + y;
        p1.z = RV(vp + 1).z * RV(vp + 1).m + z;
        p2.x = RV(vp + 2).x * RV(vp + 2).m + x;
        p2.y = RV(vp + 2).y * RV(vp + 2).m + y;
        p2.z = RV(vp + 2).z * RV(vp + 2).m + z;
        p3.x = RV(vp).x * RV(vp).m + x;
        p3.y = RV(vp).y * RV(vp).m + y;
        p3.z = RV(vp).z * RV(vp).m + z;
        U.x = p2.x - p1.x;
        U.y = p2.y - p1.y;
        U.z = p2.z - p1.z;
        V.x = p3.x - p1.x;
        V.y = p3.y - p1.y;
        V.z = p3.z - p1.z;
        o->normals[f].x = U.y * V.z - U.z * V.y;
        o->normals[f].y = U.z * V.x - U.x * V.z;
        o->normals[f].z = U.x * V.y - U.y * V.x;
        d3d_normalise(&o->normals[f]);
        ray.x = p1.x - cam->x;
        ray.y = p1.y - cam->y;
        ray.z = p1.z - cam->z;
        d3d_normalise(&ray);
        lighting.x = p1.x - o->light.x;
        lighting.y = p1.y - o->light.y;
        lighting.z = p1.z - o->light.z;
        d3d_normalise(&lighting);
        o->dots[f] = ray.x * o->normals[f].x + ray.y * o->normals[f].y + ray.z * o->normals[f].z;
        if (depthmode == 0) {
            tmp = o->r_centroids[f].m;
            D3D_FLOAT dz = o->r_centroids[f].z * tmp + z - cam->z;
            D3D_FLOAT dy = o->r_centroids[f].y * tmp + y - cam->y;
            D3D_FLOAT dx = o->r_centroids[f].x * tmp + x - cam->x;
            o->depth[f] = dz * dz + dy * dy + dx * dx;
        } else {
            D3D_FLOAT max_depth = -32767.0;
            for (int v = 0; v < o->facecount[f]; v++) {
                tmp = RV(vp + v).m;
                D3D_FLOAT dz = RV(vp + v).z * tmp + z - cam->z;
                D3D_FLOAT dy = RV(vp + v).y * tmp + y - cam->y;
                D3D_FLOAT dx = RV(vp + v).x * tmp + x - cam->x;
                D3D_FLOAT vertex_depth = dz * dz + dy * dy + dx * dx;
                if (vertex_depth > max_depth) {
                    max_depth = vertex_depth;
                }
            }
            o->depth[f] = max_depth;
        }
        o->depthindex[f] = f;
        o->distance += sqrt3d(o->depth[f]);
    }
    o->distance /= o->nf;
    depthsort(o->depth, o->nf, o->depthindex);

    // display the forward-facing faces, furthest first
    for (int f = 0; f < o->nf; f++) {
        int sortindex = o->depthindex[f];
        int vp = o->facestart[sortindex];
        if (o->flags[sortindex] & 4) {
            o->dots[sortindex] = -o->dots[sortindex];
        }
        if (nonormals || o->dots[sortindex] < 0) {
            for (int v = 0; v < o->facecount[sortindex]; v++) {
                x1 = RV(vp + v).x * RV(vp + v).m + x;
                y1 = RV(vp + v).y * RV(vp + v).m + y;
                z1 = RV(vp + v).z * RV(vp + v).m + z;
                at = x1 - cam->x;
                bt = y1 - cam->y;
                ct = z1 - cam->z;
                if (ct > -0.0005f && ct < 0.0005f) {
                    ct = (ct < 0.0f ? -0.0005f : 0.0005f);
                }
                t = -(C * z1 + D) / (C * ct);
                xcoord[v] = (short)(x1 + round3d(at * t) + (maxW >> 1) - cam->x - cam->panx);
                ycoord[v] = (short)(maxH - round3d(y1 + bt * t) - 1);
                ycoord[v] = (short)(ycoord[v] - ((maxH >> 1) - cam->y - cam->pany));
                if (clear) {
                    if (xcoord[v] > o->xmax) {
                        o->xmax = xcoord[v];
                    }
                    if (xcoord[v] < o->xmin) {
                        o->xmin = xcoord[v];
                    }
                    if (ycoord[v] > o->ymax) {
                        o->ymax = ycoord[v];
                    }
                    if (ycoord[v] < o->ymin) {
                        o->ymin = ycoord[v];
                    }
                }
            }
            if ((o->flags[sortindex] & 1) == 0) {
                if (o->flags[sortindex] & 10) {
                    fsave = o->fill[sortindex];
                    csave = o->line[sortindex];
                    if (o->flags[sortindex] & 2) {
                        o->fill[sortindex] = 0xFF0000;
                    }
                    if (o->flags[sortindex] & 8) {
                        D3D_FLOAT lightratio = fabs3d(lighting.x * o->normals[sortindex].x
                            + lighting.y * o->normals[sortindex].y
                            + lighting.z * o->normals[sortindex].z);
                        lightratio = (lightratio * o->ambient) + o->ambient;
                        for (int pass = 0; pass < 2; pass++) {
                            uint32_t *cp = pass ? &o->line[sortindex] : &o->fill[sortindex];
                            int red = (int)((D3D_FLOAT)((*cp >> 16) & 0xFF) * lightratio + 0.5);
                            int green = (int)((D3D_FLOAT)((*cp >> 8) & 0xFF) * lightratio + 0.5);
                            int blue = (int)((D3D_FLOAT)(*cp & 0xFF) * lightratio + 0.5);
                            *cp = (red << 16) | (green << 8) | blue;
                        }
                    }
                }
                d3d_draw_polygon(o, xcoord, ycoord, sortindex);
                if (o->flags[sortindex] & 10) {
                    o->fill[sortindex] = fsave;
                    o->line[sortindex] = csave;
                }
            }
        }
    }
    o->current.x = x;
    o->current.y = y;
    o->current.z = z;
    o->nonormals = nonormals;
    o->depthmode = depthmode;
    #undef RV
}

// --- sequence helpers -------------------------------------------------------
static mp_obj_t seq_item(mp_obj_t seq, size_t i) {
    return mp_obj_subscr(seq, MP_OBJ_NEW_SMALL_INT((mp_int_t)i), MP_OBJ_SENTINEL);
}

static size_t seq_len(mp_obj_t seq) {
    return (size_t)mp_obj_get_int(mp_obj_len(seq));
}

// --- module functions -------------------------------------------------------

// camera(c, viewplane, x=0, y=0, panx=0, pany=0) -- MMBasic DRAW3D CAMERA.
static mp_obj_t d3d_camera_fn(size_t n_args, const mp_obj_t *args) {
    d3d_cams_ensure();
    int n = mp_obj_get_int(args[0]);
    if (n < 1 || n > MAXCAM) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera must be 1..3"));
    }
    d3d_cams[n].viewplane = mp_obj_get_float(args[1]);
    d3d_cams[n].x = (n_args > 2) ? mp_obj_get_float(args[2]) : 0;
    d3d_cams[n].y = (n_args > 3) ? mp_obj_get_float(args[3]) : 0;
    d3d_cams[n].panx = (n_args > 4) ? mp_obj_get_float(args[4]) : 0;
    d3d_cams[n].pany = (n_args > 5) ? mp_obj_get_float(args[5]) : 0;
    d3d_cams[n].z = 0.0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_camera_obj, 2, 6, d3d_camera_fn);

// create(n, nv, nf, cam, vertices, facecount, faces, colours, edge=None,
// fill=None) -- MMBasic DRAW3D CREATE. vertices is a flat sequence of
// x,y,z per vertex; faces a flat vertex-index list in facecount order;
// edge/fill are per-face INDICES into colours (omitted: edges white, faces
// outline-only, as MMBasic).
static mp_obj_t d3d_create_fn(size_t n_args, const mp_obj_t *args) {
    d3d_cams_ensure();
    int n = mp_obj_get_int(args[0]);
    if (n < 1 || n > MAX3D) {
        mp_raise_ValueError(MP_ERROR_TEXT("object number must be 1..8"));
    }
    if (MP_STATE_PORT(draw3d_slots)[n] != NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("object already exists"));
    }
    int nv = mp_obj_get_int(args[1]);
    int nf = mp_obj_get_int(args[2]);
    int cam = mp_obj_get_int(args[3]);
    if (nv < 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("3D object must have a minimum of 3 vertices"));
    }
    if (nf < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("3D object must have a minimum of 1 face"));
    }
    if (cam < 1 || cam > MAXCAM) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera must be 1..3"));
    }
    mp_obj_t vertices = args[4], facecount = args[5], faces = args[6], colours = args[7];
    mp_obj_t edge = (n_args > 8) ? args[8] : mp_const_none;
    mp_obj_t fill = (n_args > 9) ? args[9] : mp_const_none;
    if (seq_len(vertices) < (size_t)(nv * 3)) {
        mp_raise_ValueError(MP_ERROR_TEXT("vertex array too small"));
    }
    if (seq_len(facecount) < (size_t)nf) {
        mp_raise_ValueError(MP_ERROR_TEXT("vertex count array too small"));
    }
    int total_fv = 0, vmax = 0;
    for (int f = 0; f < nf; f++) {
        int fc = mp_obj_get_int(seq_item(facecount, f));
        if (fc < 3) {
            mp_raise_ValueError(MP_ERROR_TEXT("vertex count less than 3 for a face"));
        }
        if (fc > 32) {
            mp_raise_ValueError(MP_ERROR_TEXT("more than 32 vertices in a face"));
        }
        if (fc > vmax) {
            vmax = fc;
        }
        total_fv += fc;
    }
    if (seq_len(faces) < (size_t)total_fv) {
        mp_raise_ValueError(MP_ERROR_TEXT("face/vertex array too small"));
    }
    int colourcount = (int)seq_len(colours);

    // One block: struct + all arrays.
    size_t sz = sizeof(d3d_obj_t)
        + (size_t)nv * 2 * sizeof(d3d_quat_t)
        + (size_t)nf * 2 * sizeof(d3d_quat_t)
        + (size_t)nf * sizeof(d3d_vec_t)
        + (size_t)nf * (sizeof(uint16_t) + 2 * sizeof(uint32_t) + sizeof(uint8_t) * 2
            + 2 * sizeof(D3D_FLOAT) + sizeof(int))
        + (size_t)total_fv * sizeof(uint16_t) + 16;
    uint8_t *blk = m_malloc(sz);
    memset(blk, 0, sz);
    d3d_obj_t *o = (d3d_obj_t *)blk;
    uint8_t *p = blk + sizeof(d3d_obj_t);
    #define TAKE(field, type, count) o->field = (type *)p; p += (size_t)(count) * sizeof(type)
    TAKE(q_vertices, d3d_quat_t, nv);
    TAKE(r_vertices, d3d_quat_t, nv);
    TAKE(q_centroids, d3d_quat_t, nf);
    TAKE(r_centroids, d3d_quat_t, nf);
    TAKE(normals, d3d_vec_t, nf);
    TAKE(dots, D3D_FLOAT, nf);
    TAKE(depth, D3D_FLOAT, nf);
    TAKE(depthindex, int, nf);
    TAKE(fill, uint32_t, nf);
    TAKE(line, uint32_t, nf);
    TAKE(facestart, uint16_t, nf);
    TAKE(face_x_vert, uint16_t, total_fv);
    TAKE(facecount, uint8_t, nf);
    TAKE(flags, uint8_t, nf);
    #undef TAKE

    o->nv = nv;
    o->nf = nf;
    o->cam = cam;
    o->vmax = vmax;
    o->tot_face_x_vert = total_fv;
    o->xmin = 32767;
    o->ymin = 32767;
    o->xmax = -32767;
    o->ymax = -32767;
    o->current.x = -32767;
    o->current.y = -32767;
    o->current.z = -32767;

    // vertices -> unit vector + magnitude quaternions (MMBasic CREATE).
    for (int v = 0; v < nv; v++) {
        D3D_FLOAT vx = mp_obj_get_float(seq_item(vertices, v * 3));
        D3D_FLOAT vy = mp_obj_get_float(seq_item(vertices, v * 3 + 1));
        D3D_FLOAT vz = mp_obj_get_float(seq_item(vertices, v * 3 + 2));
        D3D_FLOAT m = vx * vx + vy * vy + vz * vz;
        if (m) {
            m = sqrt3d(m);
            o->q_vertices[v].x = vx / m;
            o->q_vertices[v].y = vy / m;
            o->q_vertices[v].z = vz / m;
            o->q_vertices[v].m = m;
        } else {
            o->q_vertices[v].m = 1.0;
        }
        o->r_vertices[v] = o->q_vertices[v];
    }
    int start = 0;
    for (int f = 0; f < nf; f++) {
        o->facecount[f] = (uint8_t)mp_obj_get_int(seq_item(facecount, f));
        o->facestart[f] = (uint16_t)start;
        start += o->facecount[f];
    }
    for (int i = 0; i < total_fv; i++) {
        int vi = mp_obj_get_int(seq_item(faces, i));
        if (vi < 0 || vi >= nv) {
            mp_raise_ValueError(MP_ERROR_TEXT("face vertex index out of range"));
        }
        o->face_x_vert[i] = (uint16_t)vi;
    }
    for (int f = 0; f < nf; f++) {
        if (edge != mp_const_none) {
            int idx = mp_obj_get_int(seq_item(edge, f));
            if (idx < 0 || idx >= colourcount) {
                mp_raise_ValueError(MP_ERROR_TEXT("edge colour index"));
            }
            o->line[f] = (uint32_t)mp_obj_get_int(seq_item(colours, idx));
        } else {
            o->line[f] = 0xFFFFFF;
        }
        if (fill != mp_const_none) {
            int idx = mp_obj_get_int(seq_item(fill, f));
            if (idx < 0 || idx >= colourcount) {
                mp_raise_ValueError(MP_ERROR_TEXT("fill colour index"));
            }
            o->fill[f] = (uint32_t)mp_obj_get_int(seq_item(colours, idx));
        } else {
            o->fill[f] = 0xFFFFFFFF; // outline only
        }
        // centroid (MMBasic CREATE, verbatim)
        D3D_FLOAT cx = 0, cy = 0, cz = 0;
        int vp = o->facestart[f];
        for (int v = 0; v < o->facecount[f]; v++) {
            D3D_FLOAT tmp = o->q_vertices[o->face_x_vert[vp + v]].m;
            cx += o->q_vertices[o->face_x_vert[vp + v]].x * tmp;
            cy += o->q_vertices[o->face_x_vert[vp + v]].y * tmp;
            cz += o->q_vertices[o->face_x_vert[vp + v]].z * tmp;
        }
        cx /= o->facecount[f];
        cy /= o->facecount[f];
        cz /= o->facecount[f];
        D3D_FLOAT scale = sqrt3d(cx * cx + cy * cy + cz * cz);
        o->q_centroids[f].x = cx / scale;
        o->q_centroids[f].y = cy / scale;
        o->q_centroids[f].z = cz / scale;
        o->q_centroids[f].m = scale;
        o->r_centroids[f] = o->q_centroids[f];
    }
    MP_STATE_PORT(draw3d_slots)[n] = blk;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_create_obj, 8, 10, d3d_create_fn);

static void d3d_show_common(size_t n_args, const mp_obj_t *args, int clear) {
    int n = mp_obj_get_int(args[0]);
    D3D_FLOAT x = mp_obj_get_float(args[1]);
    D3D_FLOAT y = mp_obj_get_float(args[2]);
    D3D_FLOAT z = mp_obj_get_float(args[3]);
    int nonormals = (n_args > 4) ? mp_obj_get_int(args[4]) : 0;
    int depthmode = (n_args > 5) ? mp_obj_get_int(args[5]) : 0;
    d3d_obj_t *o = d3d_get(n, true);
    d3d_cams_ensure();
    if (d3d_cams[o->cam].viewplane == -32767) {
        mp_raise_ValueError(MP_ERROR_TEXT("camera position not defined"));
    }
    if (depthmode < 0 || depthmode > 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("depthmode 2 (hidden line) not ported yet"));
    }
    display3d(n, x, y, z, clear, nonormals, depthmode);
}

// show(n, x, y, z, nonormals=0, depthmode=0) -- erase previous, draw.
static mp_obj_t d3d_show_fn(size_t n_args, const mp_obj_t *args) {
    d3d_show_common(n_args, args, 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_show_obj, 4, 6, d3d_show_fn);

// write(n, x, y, z, ...) -- as show, without erasing the previous position.
static mp_obj_t d3d_write_fn(size_t n_args, const mp_obj_t *args) {
    d3d_show_common(n_args, args, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_write_obj, 4, 6, d3d_write_fn);

// rotate(q, n, ...) -- q is (w, x, y, z, m); rotates from the ORIGINAL
// orientation (use reset() to make rotations cumulative), as MMBasic.
static mp_obj_t d3d_rotate_fn(size_t n_args, const mp_obj_t *args) {
    d3d_quat_t q1;
    size_t qlen = seq_len(args[0]);
    if (qlen < 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("rotation quaternion needs (w,x,y,z,m)"));
    }
    q1.w = mp_obj_get_float(seq_item(args[0], 0));
    q1.x = mp_obj_get_float(seq_item(args[0], 1));
    q1.y = mp_obj_get_float(seq_item(args[0], 2));
    q1.z = mp_obj_get_float(seq_item(args[0], 3));
    q1.m = (qlen > 4) ? mp_obj_get_float(seq_item(args[0], 4)) : 1.0;
    for (size_t i = 1; i < n_args; i++) {
        d3d_obj_t *o = d3d_get(mp_obj_get_int(args[i]), true);
        for (int v = 0; v < o->nv; v++) {
            q_rotate(&o->q_vertices[v], &q1, &o->r_vertices[v]);
        }
        for (int f = 0; f < o->nf; f++) {
            q_rotate(&o->q_centroids[f], &q1, &o->r_centroids[f]);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_rotate_obj, 2, 9, d3d_rotate_fn);

// reset(n, ...) -- the rotated orientation becomes the new original.
static mp_obj_t d3d_reset_fn(size_t n_args, const mp_obj_t *args) {
    for (size_t i = 0; i < n_args; i++) {
        d3d_obj_t *o = d3d_get(mp_obj_get_int(args[i]), true);
        memcpy(o->q_vertices, o->r_vertices, o->nv * sizeof(d3d_quat_t));
        memcpy(o->q_centroids, o->r_centroids, o->nf * sizeof(d3d_quat_t));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_reset_obj, 1, 8, d3d_reset_fn);

static void d3d_hide_one(d3d_obj_t *o) {
    if (o->xmin != 32767) {
        hdmi_fill_rect_raw(o->xmin, o->ymin, o->xmax, o->ymax, 0);
        o->xmin = 32767;
        o->ymin = 32767;
        o->xmax = -32767;
        o->ymax = -32767;
    }
}

// hide(n, ...) -- erase the object's bounding box (black), as MMBasic.
static mp_obj_t d3d_hide_fn(size_t n_args, const mp_obj_t *args) {
    for (size_t i = 0; i < n_args; i++) {
        d3d_hide_one(d3d_get(mp_obj_get_int(args[i]), true));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_hide_obj, 1, 8, d3d_hide_fn);

static mp_obj_t d3d_hide_all_fn(void) {
    for (int n = 1; n <= MAX3D; n++) {
        d3d_obj_t *o = d3d_get(n, false);
        if (o != NULL) {
            d3d_hide_one(o);
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(d3d_hide_all_obj, d3d_hide_all_fn);

// restore(n, ...) -- redraw a hidden object exactly as it last appeared.
static mp_obj_t d3d_restore_fn(size_t n_args, const mp_obj_t *args) {
    for (size_t i = 0; i < n_args; i++) {
        int n = mp_obj_get_int(args[i]);
        d3d_obj_t *o = d3d_get(n, true);
        if (o->xmin != 32767) {
            mp_raise_ValueError(MP_ERROR_TEXT("object is not hidden"));
        }
        display3d(n, o->current.x, o->current.y, o->current.z, 1,
            o->nonormals, o->depthmode);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_restore_obj, 1, 8, d3d_restore_fn);

// close(n, ...) / close_all() -- erase and free.
static mp_obj_t d3d_close_fn(size_t n_args, const mp_obj_t *args) {
    for (size_t i = 0; i < n_args; i++) {
        int n = mp_obj_get_int(args[i]);
        d3d_obj_t *o = d3d_get(n, true);
        d3d_hide_one(o);
        MP_STATE_PORT(draw3d_slots)[n] = NULL; // the GC reclaims the block
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_close_obj, 1, 8, d3d_close_fn);

static mp_obj_t d3d_close_all_fn(void) {
    d3d_cams_ensure();
    for (int n = 1; n <= MAX3D; n++) {
        MP_STATE_PORT(draw3d_slots)[n] = NULL;
    }
    for (int i = 1; i <= MAXCAM; i++) {
        d3d_cams[i].viewplane = -32767;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(d3d_close_all_obj, d3d_close_all_fn);

// set_flags(n, flag, face, nbr) -- MMBasic DRAW3D SET FLAGS: apply `flag`
// to `nbr` faces starting at `face` (1 hide, 2 red, 4 invert normal,
// 8 lighting).
static mp_obj_t d3d_set_flags_fn(size_t n_args, const mp_obj_t *args) {
    d3d_obj_t *o = d3d_get(mp_obj_get_int(args[0]), true);
    int flag = mp_obj_get_int(args[1]);
    int face = mp_obj_get_int(args[2]);
    int nbr = mp_obj_get_int(args[3]);
    if (face < 0 || nbr <= 0 || nbr > o->nf - face) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad face range"));
    }
    while (--nbr >= 0) {
        o->flags[face + nbr] = (uint8_t)flag;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_set_flags_obj, 4, 4, d3d_set_flags_fn);

// light(n, x, y, z, ambient) -- MMBasic DRAW3D LIGHT (ambient 0..100).
static mp_obj_t d3d_light_fn(size_t n_args, const mp_obj_t *args) {
    d3d_obj_t *o = d3d_get(mp_obj_get_int(args[0]), true);
    o->light.x = mp_obj_get_float(args[1]);
    o->light.y = mp_obj_get_float(args[2]);
    o->light.z = mp_obj_get_float(args[3]);
    o->ambient = mp_obj_get_float(args[4]) / 100.0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_light_obj, 5, 5, d3d_light_fn);

// q_create(theta, x, y, z) -- MMBasic MATH Q_CREATE: a normalised rotation
// quaternion as the (w, x, y, z, m) tuple rotate() takes. theta in radians.
// Double precision, as MMBasic's MATH commands (MMFLOAT) -- only the 3D
// engine itself runs single.
static mp_obj_t d3d_q_create_fn(size_t n_args, const mp_obj_t *args) {
    double theta = mp_obj_get_float(args[0]);
    double x = mp_obj_get_float(args[1]);
    double y = mp_obj_get_float(args[2]);
    double z = mp_obj_get_float(args[3]);
    double sineterm = sin(theta / 2.0);
    double q0 = cos(theta / 2.0);
    double q1 = x * sineterm, q2 = y * sineterm, q3 = z * sineterm;
    double mag = sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    mp_obj_t t[5] = {
        mp_obj_new_float(q0 / mag), mp_obj_new_float(q1 / mag),
        mp_obj_new_float(q2 / mag), mp_obj_new_float(q3 / mag),
        mp_obj_new_float(1.0),
    };
    return mp_obj_new_tuple(5, t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(d3d_q_create_obj, 4, 4, d3d_q_create_fn);

// query(n, what) -- the DRAW3D() function: "xmin"/"xmax"/"ymin"/"ymax"
// (last drawn bounding box), "x"/"y"/"z" (last position), "distance".
static mp_obj_t d3d_query_fn(mp_obj_t n_in, mp_obj_t what_in) {
    d3d_obj_t *o = d3d_get(mp_obj_get_int(n_in), true);
    const char *w = mp_obj_str_get_str(what_in);
    if (strcmp(w, "xmin") == 0) {
        return MP_OBJ_NEW_SMALL_INT(o->xmin);
    }
    if (strcmp(w, "xmax") == 0) {
        return MP_OBJ_NEW_SMALL_INT(o->xmax);
    }
    if (strcmp(w, "ymin") == 0) {
        return MP_OBJ_NEW_SMALL_INT(o->ymin);
    }
    if (strcmp(w, "ymax") == 0) {
        return MP_OBJ_NEW_SMALL_INT(o->ymax);
    }
    if (strcmp(w, "x") == 0) {
        return mp_obj_new_float(o->current.x);
    }
    if (strcmp(w, "y") == 0) {
        return mp_obj_new_float(o->current.y);
    }
    if (strcmp(w, "z") == 0) {
        return mp_obj_new_float(o->current.z);
    }
    if (strcmp(w, "distance") == 0) {
        return mp_obj_new_float(o->distance);
    }
    mp_raise_ValueError(MP_ERROR_TEXT("unknown draw3d query"));
}
static MP_DEFINE_CONST_FUN_OBJ_2(d3d_query_obj, d3d_query_fn);

static const mp_rom_map_elem_t draw3d_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_draw3d) },
    { MP_ROM_QSTR(MP_QSTR_camera), MP_ROM_PTR(&d3d_camera_obj) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&d3d_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&d3d_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&d3d_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_rotate), MP_ROM_PTR(&d3d_rotate_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&d3d_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_hide), MP_ROM_PTR(&d3d_hide_obj) },
    { MP_ROM_QSTR(MP_QSTR_hide_all), MP_ROM_PTR(&d3d_hide_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_restore), MP_ROM_PTR(&d3d_restore_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&d3d_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_close_all), MP_ROM_PTR(&d3d_close_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_flags), MP_ROM_PTR(&d3d_set_flags_obj) },
    { MP_ROM_QSTR(MP_QSTR_light), MP_ROM_PTR(&d3d_light_obj) },
    { MP_ROM_QSTR(MP_QSTR_q_create), MP_ROM_PTR(&d3d_q_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_query), MP_ROM_PTR(&d3d_query_obj) },
};
static MP_DEFINE_CONST_DICT(draw3d_module_globals, draw3d_module_globals_table);

const mp_obj_module_t draw3d_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&draw3d_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_draw3d, draw3d_module);

#endif // MICROPY_HW_ENABLE_HDMI
