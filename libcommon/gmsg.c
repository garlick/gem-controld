/*****************************************************************************\
 *  Copyright (C) 2015 Jim Garlick
 *  Written by Jim Garlick <garlick.jim@gmail.com>
 *  All Rights Reserved.
 *
 *  This file is part of gem-controld
 *  For details, see <https://github.com/garlick/gem-controld>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#include <stdbool.h>
#include <stdlib.h>
#include <czmq.h>

#include "libutil/xzmalloc.h"
#include "gmsg.h"

#define PROTO_SIZE      16
#define PROTO_OFF_OP    0   /* 1 byte */
                            /* 3 bytes (unused) */
#define PROTO_OFF_FLAGS 4   /* 4 bytes */
#define PROTO_OFF_ARG1  8   /* 4 bytes */
#define PROTO_OFF_ARG2  12  /* 4 bytes */

struct gmsg_struct {
    zmsg_t *zmsg;
};

static void set_op (uint8_t *proto, uint8_t op)
{
    proto[PROTO_OFF_OP] = op;
}

static void get_op (uint8_t *proto, uint8_t *op)
{
    *op = proto[PROTO_OFF_OP];
}

static void set_flags (uint8_t *proto, uint32_t flags)
{
    uint32_t x = htonl (flags);
    memcpy (&proto[PROTO_OFF_FLAGS], &x, sizeof (x));
}

static void get_flags (uint8_t *proto, uint32_t *flags)
{
    uint32_t x;
    memcpy (&x, &proto[PROTO_OFF_FLAGS], sizeof (x));
    *flags = ntohl (x);
}

static void set_arg1 (uint8_t *proto, int32_t arg)
{
    int32_t x = htonl (arg);
    memcpy (&proto[PROTO_OFF_ARG1], &x, sizeof (x));
}

static void get_arg1 (uint8_t *proto, int32_t *arg)
{
    int32_t x;
    memcpy (&x, &proto[PROTO_OFF_ARG1], sizeof (x));
    *arg = ntohl (x);
}

static void set_arg2 (uint8_t *proto, int32_t arg)
{
    int32_t x = htonl (arg);
    memcpy (&proto[PROTO_OFF_ARG2], &x, sizeof (x));
}

static void get_arg2 (uint8_t *proto, int32_t *arg)
{
    int32_t x;
    memcpy (&x, &proto[PROTO_OFF_ARG2], sizeof (x));
    *arg = ntohl (x);
}

gmsg_t gmsg_create (uint8_t op)
{
    uint8_t proto[PROTO_SIZE];
    gmsg_t g = xzmalloc (sizeof (*g));

    memset (proto, 0, PROTO_SIZE);
    set_op (proto, op);
    if (!(g->zmsg = zmsg_new ())) {
        errno = ENOMEM;
        goto done;
    }
    if (zmsg_addmem (g->zmsg, proto, PROTO_SIZE) < 0) {
        gmsg_destroy (&g);
        goto done;
    }
    /* Add dealer-router route delimiter.
     */
    if (zmsg_pushmem (g->zmsg, NULL, 0) < 0) {
        gmsg_destroy (&g);
        goto done;
    }
done:
    return g;
}

void gmsg_destroy (gmsg_t *g)
{
    if (*g) {
        zmsg_destroy (&(*g)->zmsg);
        free (*g);
        *g = NULL;
    }
}

int gmsg_send (void *sock, gmsg_t g)
{
    zmsg_t *z = zmsg_dup (g->zmsg);
    int rc = zmsg_send (&z, sock);
    zmsg_destroy (&z);
    return rc;
}

gmsg_t gmsg_recv (void *sock)
{
    zmsg_t *z = zmsg_recv (sock);
    gmsg_t g = NULL;
    if (z) {
        g = xzmalloc (sizeof (*g));
        g->zmsg = z;
    }
    return g;
}


int gmsg_set_op (gmsg_t g, uint8_t op)
{
    int rc = -1;
    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EINVAL;
        goto done;
    }
    set_op (zframe_data (zf), op);
    rc = 0;
done:
    return rc;
}

int gmsg_get_op (gmsg_t g, uint8_t *op)
{
    int rc = -1;
    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EPROTO;
        goto done;
    }
    get_op (zframe_data (zf), op);
    rc = 0;
done:
    return rc;
}

int gmsg_set_flags (gmsg_t g, uint32_t flags)
{
    int rc = -1;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EINVAL;
        goto done;
    }
    set_flags (zframe_data (zf), flags);
    rc = 0;
done:
    return rc;
}

int gmsg_get_flags (gmsg_t g, uint32_t *flags)
{
    int rc = -1;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EPROTO;
        goto done;
    }
    get_flags (zframe_data (zf), flags);
    rc = 0;
done:
    return rc;
}

int gmsg_set_arg1 (gmsg_t g, int32_t arg)
{
    uint32_t flags;
    int rc = -1;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EINVAL;
        goto done;
    }
    set_arg1 (zframe_data (zf), arg);
    get_flags (zframe_data (zf), &flags);
    flags |= FLAG_ARG1;
    set_flags (zframe_data (zf), flags);
    rc = 0;
done:
    return rc;
}

int gmsg_get_arg1 (gmsg_t g, int32_t *arg)
{
    uint32_t flags;
    int rc = -1;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EPROTO;
        goto done;
    }
    get_flags (zframe_data (zf), &flags);
    if (!(flags & FLAG_ARG1)) {
        errno = EPROTO;
        goto done;
    }
    get_arg1 (zframe_data (zf), arg);
    rc = 0;
done:
    return rc;
}

int gmsg_set_arg2 (gmsg_t g, int32_t arg)
{
    uint32_t flags;
    int rc = -1;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EINVAL;
        goto done;
    }
    set_arg2 (zframe_data (zf), arg);
    get_flags (zframe_data (zf), &flags);
    flags |= FLAG_ARG2;
    set_flags (zframe_data (zf), flags);
    rc = 0;
done:
    return rc;
}

int gmsg_get_arg2 (gmsg_t g, int32_t *arg)
{
    uint32_t flags;
    int rc = -1;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EPROTO;
        goto done;
    }
    get_flags (zframe_data (zf), &flags);
    if (!(flags & FLAG_ARG2)) {
        errno = EPROTO;
        goto done;
    }
    get_arg2 (zframe_data (zf), arg);
    rc = 0;
done:
    return rc;
}

int gmsg_error (gmsg_t g)
{
    int rc = -1;
    uint32_t flags;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EINVAL;
        goto done;
    }
    get_flags (zframe_data (zf), &flags);
    if ((flags & FLAG_ERROR)) {
        get_arg1 (zframe_data (zf), &errno);
        goto done;
    }
    rc = 0;
done:
    return rc;
}

void gmsg_dump (FILE *f, gmsg_t g, const char *prefix)
{
    uint8_t op;
    uint32_t flags;
    int32_t arg1, arg2;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        fprintf (f, "%s: mangled\n", prefix);
        return;
    }
    get_op (zframe_data (zf), &op);
    get_flags (zframe_data (zf), &flags);
    get_arg1 (zframe_data (zf), &arg1);
    get_arg2 (zframe_data (zf), &arg2);
    fprintf (f, "%s: [%d] 0x%.4x %d %d\n", prefix, op, flags, arg1, arg2);
}

int gmsg_set_error (gmsg_t g, int32_t errnum)
{
    uint32_t flags;
    int rc = -1;

    zframe_t *zf = zmsg_last (g->zmsg);
    if (!zf || zframe_size (zf) != PROTO_SIZE) {
        errno = EINVAL;
        goto done;
    }
    set_arg1 (zframe_data (zf), errnum);
    get_flags (zframe_data (zf), &flags);
    flags |= FLAG_ERROR;
    set_flags (zframe_data (zf), flags);
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
