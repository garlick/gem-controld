/*****************************************************************************\
 *  Copyright (C) 2017 Jim Garlick
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <ev.h>

#include "log.h"
#include "xzmalloc.h"

#include "bbox.h"

#define LISTEN_BACKLOG 5
#define MAX_CLIENTS 16
#define MAX_COMMAND_BYTES 32

struct client {
    int fd;
    ev_io w;
    char buf[MAX_COMMAND_BYTES];
    int len;
    struct bbox *bb;
    int num;
};

struct bbox {
    int flags;
    int port;
    int fd;
    bbox_cb_t cb;
    void *cb_arg;
    ev_io listen_w;
    struct client clients[MAX_CLIENTS];
    int x, y;
    int x_res, y_res;
    double x_scale, y_scale;
    struct ev_loop *loop;
};

static void client_free (struct client *c);

static int write_all (int fd, char *buf, int len)
{
    int n, done = 0;

    while (done < len) {
        n = write (fd, buf, len - done);
        if (n < 0)
            return -1;
        done += n;
    }
    return len;
}

static void client_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    struct client *c = (struct client *)((char *)w
                        - offsetof (struct client, w));
    int n;

    n = read (c->fd, c->buf + c->len, sizeof (c->buf) - c->len);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            goto out;
        goto disconnect;
    }
    if (n == 0) // EOF
        goto disconnect;
    c->len += n;

    if ((c->bb->flags & BBOX_DEBUG))
        msg ("%s[%d]: received '%.*s'", __FUNCTION__, c->num, c->len, c->buf);

    /* get device position
     * > Q
     * < +04512\t-01297\r         ; encoder X followed by encoder Y (13b + \r)
     * N.B. Multiple Q characters are sometimes sent in a row to "wake up"
     * the device, so allow up to MAX_COMMAND_BYTES of them to be treated
     * as on "Q" command.
     */
    if (c->len >= 1 && c->buf[0] == 'Q') {
        char buf[32];

        if ((c->bb->flags & BBOX_DEBUG))
            msg ("%s[%d]: matched Q command", __FUNCTION__, c->num);
        if (c->bb->cb)
            c->bb->cb (c->bb, c->bb->cb_arg);
        snprintf (buf, sizeof (buf), "%+.5d\t%+.5d\r", c->bb->x, c->bb->y);
        if (write_all (c->fd, buf, strlen (buf)) < 0) {
            goto disconnect;
        }
        if ((c->bb->flags & BBOX_DEBUG))
            msg ("%s[%d]: wrote %s", __FUNCTION__, c->num, buf);
        goto out_clear;
    }
    /* get encoder resolutions
     * > H
     * < +8192\t+8192\r          ; encoder X and Y max tics + \r
     */
    if (c->len >= 1 && c->buf[0] == 'H') {
        char buf[32];

        if ((c->bb->flags & BBOX_DEBUG))
            msg ("%s[%d]: matched H command", __FUNCTION__, c->num);
        snprintf (buf, sizeof (buf), "%+.5d\t%+.5d\r",
                  c->bb->x_res, c->bb->y_res);
        if (write_all (c->fd, buf, strlen (buf)) < 0) {
            err ("%s[%d]: write error", __FUNCTION__, c->num);
            goto disconnect;
        }
        if ((c->bb->flags & BBOX_DEBUG))
            msg ("%s[%d]: wrote %s", __FUNCTION__, c->num, buf);
        goto out_clear;
    }
    /* no match
     */
    else {
        if ((c->bb->flags & BBOX_DEBUG))
            msg ("%s[%d]: no match, discarding", __FUNCTION__, c->num);
        goto out_clear;
    }

out_clear:
    c->len = 0;
out:
    return;
disconnect:
    client_free (c);
}

static void client_free (struct client *c)
{
    if (c->fd != -1) {
        close (c->fd);
        c->fd = -1;
        ev_io_stop (c->bb->loop, &c->w);
    }
}

static struct client *client_alloc (struct bbox *bb, int fd)
{
    int i;
    struct client *c;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (bb->clients[i].fd == -1)
            break;
    }
    if (i == MAX_CLIENTS)
        return NULL; // no client slot
    c = &bb->clients[i];
    c->fd = fd;
    ev_io_init (&c->w, client_cb, c->fd, EV_READ);
    ev_io_start (c->bb->loop, &c->w);
    return c;
}

/* Accept a connection and allocate client slot.
 */
static void listen_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    struct bbox *bb = (struct bbox *)((char *)w
                        - offsetof (struct bbox, listen_w));

    if ((revents & EV_ERROR)) {
    }

    if ((revents & EV_READ)) {
        int cfd;
        struct client *c;
        if ((cfd = accept4 (bb->fd, NULL, NULL, SOCK_CLOEXEC)) < 0)
            return;
        if (!(c = client_alloc (bb, cfd))) { // too many open connections
            close (cfd);
            return;
        }
        if ((bb->flags & BBOX_DEBUG))
            msg ("%s[%d]: client starting", __FUNCTION__, c->num);
    }
}

/* N.B. Try to mimic 16-bit Tangent/BBOX limitations.
 * Keep values well under +/-32K to avoid under/overflow issues,
 * or exceeding 5 digits of precision on the wire.
 */
static int scale_resolution (int res, double *scale)
{
    if (res < 16384) {
        *scale = 1.;
    }
    else {
        *scale = 16384. / res;
        res = 16384;
    }
    return res;
}

/* Resolution is the number of steps in 360 degree rotation.
 * The angle in degrees is 360 * (position / resolution)
 */
void bbox_set_resolution (struct bbox *bb, int x, int y)
{
    bb->x_res = scale_resolution (labs (x), &bb->x_scale);
    bb->y_res = scale_resolution (labs (y), &bb->y_scale);;
}

void bbox_set_position (struct bbox *bb, int x, int y)
{
    bb->x = bb->x_scale * x;
    bb->y = bb->y_scale * y;
}

int bbox_init (struct bbox *bb, int port, bbox_cb_t cb, void *arg, int flags)
{
    struct sockaddr_in addr;

    bb->cb = cb;
    bb->cb_arg = arg;
    bb->flags = flags;

    bb->fd = socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (bb->fd < 0)
        return -1;
    memset (&addr, 0, sizeof (struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons (port);
    if (bind (bb->fd, (struct sockaddr *)&addr, sizeof (addr)) < 0)
        return -1;
    if (listen (bb->fd, LISTEN_BACKLOG) < 0)
        return -1;

    ev_io_init (&bb->listen_w, listen_cb, bb->fd, EV_READ);

    if ((bb->flags & BBOX_DEBUG))
        msg ("listening on port %d", port);

    return 0;
}

void bbox_start (struct ev_loop *loop, struct bbox *bb)
{
    int i;

    bb->loop = loop;
    ev_io_start (loop, &bb->listen_w);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (bb->clients[i].fd != -1)
            ev_io_start (loop, &bb->clients[i].w);
    }
}

void bbox_stop (struct ev_loop *loop, struct bbox *bb)
{
    int i;

    ev_io_stop (loop, &bb->listen_w);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (bb->clients[i].fd != -1)
            ev_io_stop (loop, &bb->clients[i].w);
    }
}

struct bbox *bbox_new (void)
{
    struct bbox *bb = xzmalloc (sizeof (*bb));
    int i;

    for (i = 0; i < MAX_CLIENTS; i++) {
        bb->clients[i].fd = -1;
        bb->clients[i].bb = bb;
        bb->clients[i].num = i;
    }
    bb->fd = -1;

    return bb;
}

void bbox_destroy (struct bbox *bb)
{
    int i;

    if (bb) {
        for (i = 0; i < MAX_CLIENTS; i++)
            client_free (&bb->clients[i]);
        if (bb->fd != -1)
            close (bb->fd);
        free (bb);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
