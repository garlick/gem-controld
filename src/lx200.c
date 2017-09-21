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

/* Ref: Meade Telescope Serial Command Protocol Revision L, 9 October 2002.
 * https://www.meade.com/support/LX200CommandSet.pdf
 */

/* Sky Safari sends the following commands:
 *
 * If "Set time and location" checked:
 *  St      Set site latitude
 *  Sg      Set site longitude
 *  SG      Set local time zone
 *  SL      Set local time
 *  SC      Set local date
 *
 * Then:
 *  GR      Get telescope RA
 *  RM      Set slew rate to "find" rate (2nd fastest)
 *  GD#     Get telescope DEC
 *
 * Poll for current position:
 *  GR      Get telescope RA
 *  GD#     Get telescope DEC
 *
 * Slew (buttons):
 *  Me Mw Mn Ms   Slew in a particular direction
 *  Qe Qw Qn Qs   Halt slew in a particular direction
 *
 * Sync:
 * Sdr      Set target RA
 * Sds      Set target DEC
 * CM       Sync telescope wtih target
 *
 * Goto:
 * Sdr      Set target RA
 * Sds      Set target DEC
 * MS       Slew to target
 * Q        Halt all slewing
 */

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
#include <assert.h>

#include "log.h"
#include "xzmalloc.h"
#include "util.h"

#include "lx200.h"

#define LISTEN_BACKLOG 5
#define MAX_CLIENTS 16
#define MAX_COMMAND_BYTES 64

#define LX200_ACK   0x6
#define LX200_NAK   0x15

struct client {
    int fd;
    ev_io w;
    char buf[MAX_COMMAND_BYTES];
    int len;
    struct lx200 *lx;
    int num;
};

struct lx200 {
    int flags;
    int port;
    int fd;
    lx200_cb_t pos_cb;
    void *pos_cb_arg;
    lx200_cb_t slew_cb;
    void *slew_cb_arg;
    ev_io listen_w;
    struct client clients[MAX_CLIENTS];
    double t, d; // axis angular position (degrees)
    int slew_mask;
    struct util *util;
    struct ev_loop *loop;
};

static void client_free (struct client *c);

static int write_all (struct client *c, char *buf, int len)
{
    int n, done = 0;

    if ((c->lx->flags & LX200_DEBUG))
        msg ("client[%d]: < '%.*s'", c->num, len, buf);

    while (done < len) {
        n = write (c->fd, buf, len - done);
        if (n < 0)
            return -1;
        done += n;
    }
    return len;
}

static int wpf (struct client *c, const char *fmt, ...)
{
    va_list ap;
    char buf[64];
    int rc;

    va_start (ap, fmt);
    rc = vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    if (rc < 0)
        return -1;
    return write_all (c, buf, strlen (buf));
}

/* Return 0 on success, -1 on error.
 * Returning -1 causes a disconnect, so don't do it when error can
 * be returned in the command response to the client.
 */
static int process_command (struct client *c, const char *cmd)
{
    int rc = 0;
    int new_slew_mask = c->lx->slew_mask;

    if ((c->lx->flags & LX200_DEBUG))
        msg ("client[%d]: > '%s'", c->num, cmd);

    /* :StsDD*MM# - Set site latitude to sDD*MM
     */
    if (!strncmp (cmd, ":St", 3)) {
        int deg, min;
        if (sscanf (cmd + 3, "%d*%d#", &deg, &min) == 2) {
            util_set_latitude (c->lx->util, deg, min, 0.);
            rc = write_all (c, "1", 1);
        }
        else
            rc = write_all (c, "0", 1);
    }
    /* :SgDDD*MM# - Set site longitude to DDD*MM
     */
    else if (!strncmp (cmd, ":Sg", 3)) {
        int deg, min;
        if (sscanf (cmd + 3, "%d*%d#", &deg, &min) == 2) {
            util_set_longitude (c->lx->util, deg, min, 0.);
            rc = write_all (c, "1", 1);
        }
        else
            rc = write_all (c, "0", 1);
    }
    /* :SGsHH.H# - Set num hours added to local time to yield UTC
     */
    else if (!strncmp (cmd, ":SG", 3)) {
        rc = write_all (c, "1", 1);
    }
    /* :SLHH:MM:SS# - Set the local time
     */
    else if (!strncmp (cmd, ":SL", 3)) {
        rc = write_all (c, "1", 1);
    }
    /* :SCMM/DD/YY# - Set the local date
     */
    else if (!strncmp (cmd, ":SC", 3)) {
        rc = wpf (c, "1%s#", "Updating Planetary Data");
    }
    /* :RM# - Set slew rate to Find Rate (2nd fastest)
     */
    else if (!strcmp (cmd, ":RM#")) {
        // no response
    }
    /* :GR# - Get telescope RA
     */
    else if (!strcmp (cmd, ":GR#")) {
        int hr, min;
        double sec;
        if (c->lx->pos_cb)
            c->lx->pos_cb (c->lx, c->lx->pos_cb_arg); // update position t,d
        util_set_position (c->lx->util, c->lx->t, c->lx->d);
        util_get_position_ra (c->lx->util, &hr, &min, &sec);
        rc = wpf (c, "%.2d:%.2d:%.2d#", hr, min, (int)sec);
    }
    /* :GD# - Get telescope DEC
     */
    else if (!strcmp (cmd, ":GD#")) {
        int deg, min;
        double sec;
        if (c->lx->pos_cb)
            c->lx->pos_cb (c->lx, c->lx->pos_cb_arg); // update position t,d
        util_set_position (c->lx->util, c->lx->t, c->lx->d);
        util_get_position_dec (c->lx->util, &deg, &min, &sec);
        rc = wpf (c, "%+.2d*%.2d'%.2d#", deg, min, (int)sec);
    }
    /* :Me#, :Mw#, :Mn#, or :Ms# - slew east, west, north, or south
     * :Qe#, :Qw#, :Qn#, or :Qs# - stop slew in specified direction
     * :Q# - stop all slewing
     * (no response)
     */
    else if (!strcmp (cmd, ":Me#"))
        new_slew_mask |= LX200_SLEW_EAST;
    else if (!strcmp (cmd, ":Mw#"))
        new_slew_mask |= LX200_SLEW_WEST;
    else if (!strcmp (cmd, ":Mn#"))
        new_slew_mask |= LX200_SLEW_NORTH;
    else if (!strcmp (cmd, ":Ms#"))
        new_slew_mask |= LX200_SLEW_SOUTH;
    else if (!strcmp (cmd, ":Qe#"))
        new_slew_mask &= ~LX200_SLEW_EAST;
    else if (!strcmp (cmd, ":Qw#"))
        new_slew_mask &= ~LX200_SLEW_WEST;
    else if (!strcmp (cmd, ":Qn#"))
        new_slew_mask &= ~LX200_SLEW_NORTH;
    else if (!strcmp (cmd, ":Qs#"))
        new_slew_mask &= ~LX200_SLEW_SOUTH;
    else if (!strcmp (cmd, ":Q#"))
        new_slew_mask = 0;
    /* :SrHH:MM.T# or :SrHH:MM:SS# - set target object RA
     */
    else if (!strncmp (cmd, ":Sr", 3)) {
        int hr, min, sec, tenths;
        if (sscanf (cmd + 3, "%d:%d:%d#", &hr, &min, &sec) == 3) {
            util_set_target_ra (c->lx->util, hr, min, sec);
            rc = write_all (c, "1", 1);
        }
        else if (sscanf (cmd + 3, "%d:%d.%d#", &hr, &min, &tenths) == 2) {
            util_set_target_dec (c->lx->util, hr, min, 6*tenths);
            rc = write_all (c, "1", 1);
        }
        else
            rc = write_all (c, "0", 1);
    }
    /* :SdsDD*MM# or :SdsDD*MM:SS# - set target object DEC
     */
    else if (!strncmp (cmd, ":Sd", 3)) {
        int deg, min, sec;
        if (sscanf (cmd + 3, "%d*%d:%d#", &deg, &min, &sec) == 3) {
            util_set_target_dec (c->lx->util, deg, min, sec);
            rc = write_all (c, "1", 1);
        }
        else if (sscanf (cmd + 3, "%d*%d#", &deg, &min) == 2) {
            util_set_target_dec (c->lx->util, deg, min, 0);
            rc = write_all (c, "1", 1);
        }
        else
            rc = write_all (c, "0", 1);
    }
    /* :CM# - sync telescope's position with currently slected db object coord
     */
    else if (!strcmp (cmd, ":CM#")) {
        if (c->lx->pos_cb)
            c->lx->pos_cb (c->lx, c->lx->pos_cb_arg); // update position t,d
        util_set_position (c->lx->util, c->lx->t, c->lx->d);
        util_sync_target (c->lx->util);
        rc = wpf (c, "You Are Here#");
    }
    /* :MS# - slew to target object
     */
    else if (!strcmp (cmd, ":MS#")) {
        rc = write_all (c, "0", 1); // success "slew is possible"
        /* N.B. 1<string># obj below horizon; 2<string># below ... something */
    }

    /* Slew commands trigger callback if mask changed
     */
    if (c->lx->slew_mask != new_slew_mask) {
        c->lx->slew_mask = new_slew_mask;
        if (c->lx->slew_cb)
            c->lx->slew_cb (c->lx, c->lx->slew_cb_arg);
    }

    /* Ignore command if it is not recognized.
     * Protocol document is not clear on what to do here.
     */

    return rc;
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

    /* ACK - alignment query
     * This command is not framed like the others.
     * Returns 'A' alt-az, 'L' land, or 'P' polar
     */
    if (c->len > 0 && c->buf[0] == LX200_ACK) {
        if (c->len > 0 && (c->lx->flags & LX200_DEBUG))
            msg ("%s[%d]: received 0x%x", __FUNCTION__, c->num, c->buf[0]);
        memmove (c->buf, &c->buf[1], --c->len);
        if (write_all (c, "P", 1) < 0)
            goto disconnect;
    }

    while (c->len > 0) {
        char cmd[MAX_COMMAND_BYTES], *term;
        int cmdlen;
        /* Framing: first char must be ':'.
         * Discard anything before that.
         */
        while (c->len > 0 && c->buf[0] != ':') {
            if ((c->lx->flags & LX200_DEBUG))
                msg ("%s[%d]: dropping received 0x%x", __FUNCTION__, c->num,
                     c->buf[0]);
            memmove (&c->buf[0], &c->buf[1], --c->len);
        }
        /* Framing: look for '#' command termination.
         * If none, perhaps more is on the way.
         */
        if (c->len < 2 || !(term = memchr (c->buf, '#', c->len)))
            goto out;
        cmdlen = term - c->buf + 1;
        memcpy (cmd, c->buf, cmdlen);
        memmove (c->buf, term + 1, c->len -= cmdlen);
        assert (cmdlen < MAX_COMMAND_BYTES);
        cmd[cmdlen] = '\0';
        if (process_command (c, cmd) < 0)
            goto disconnect;
    }
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
        ev_io_stop (c->lx->loop, &c->w);
    }
}

static struct client *client_alloc (struct lx200 *lx, int fd)
{
    int i;
    struct client *c;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (lx->clients[i].fd == -1)
            break;
    }
    if (i == MAX_CLIENTS)
        return NULL; // no client slot
    c = &lx->clients[i];
    c->fd = fd;
    ev_io_init (&c->w, client_cb, c->fd, EV_READ);
    ev_io_start (c->lx->loop, &c->w);
    return c;
}

/* Accept a connection and allocate client slot.
 */
static void listen_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    struct lx200 *lx = (struct lx200 *)((char *)w
                        - offsetof (struct lx200, listen_w));

    if ((revents & EV_ERROR)) {
    }

    if ((revents & EV_READ)) {
        int cfd;
        struct client *c;
        if ((cfd = accept4 (lx->fd, NULL, NULL, SOCK_CLOEXEC)) < 0)
            return;
        if (!(c = client_alloc (lx, cfd))) { // too many open connections
            close (cfd);
            return;
        }
    }
}

static void slew_dump (int val)
{
    msg ("lx200 slew: (0x%x) %sN %sS %sE %sW", val,
         (val & LX200_SLEW_NORTH) ? "*" : " ",
         (val & LX200_SLEW_SOUTH) ? "*" : " ",
         (val & LX200_SLEW_EAST) ? "*" : " ",
         (val & LX200_SLEW_WEST) ? "*" : " ");
}

int lx200_get_slew (struct lx200 *lx)
{
    if ((lx->flags & LX200_DEBUG))
        slew_dump (lx->slew_mask);
    return lx->slew_mask;
}

void lx200_set_slew_cb  (struct lx200 *lx, lx200_cb_t cb, void *arg)
{
    lx->slew_cb = cb;
    lx->slew_cb_arg = arg;
}

void lx200_set_position (struct lx200 *lx, double t, double d)
{
    lx->t = t;
    lx->d = d;
}

void lx200_set_position_cb  (struct lx200 *lx, lx200_cb_t cb, void *arg)
{
    lx->pos_cb = cb;
    lx->pos_cb_arg = arg;
}

int lx200_init (struct lx200 *lx, int port, int flags)
{
    struct sockaddr_in addr;

    lx->flags = flags;

    lx->fd = socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lx->fd < 0)
        return -1;
    memset (&addr, 0, sizeof (struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons (port);
    if (bind (lx->fd, (struct sockaddr *)&addr, sizeof (addr)) < 0)
        return -1;
    if (listen (lx->fd, LISTEN_BACKLOG) < 0)
        return -1;

    ev_io_init (&lx->listen_w, listen_cb, lx->fd, EV_READ);

    if ((lx->flags & LX200_DEBUG))
        msg ("listening on port %d", port);

    if ((lx->flags & LX200_DEBUG))
        util_set_flags (lx->util, UTIL_DEBUG);

    return 0;
}

void lx200_start (struct ev_loop *loop, struct lx200 *lx)
{
    int i;

    lx->loop = loop;
    ev_io_start (loop, &lx->listen_w);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (lx->clients[i].fd != -1)
            ev_io_start (loop, &lx->clients[i].w);
    }
}

void lx200_stop (struct ev_loop *loop, struct lx200 *lx)
{
    int i;

    ev_io_stop (loop, &lx->listen_w);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (lx->clients[i].fd != -1)
            ev_io_stop (loop, &lx->clients[i].w);
    }
}

struct lx200 *lx200_new (void)
{
    struct lx200 *lx = xzmalloc (sizeof (*lx));
    int i;

    if (!(lx->util = util_new ()))
        err_exit ("util_create");

    for (i = 0; i < MAX_CLIENTS; i++) {
        lx->clients[i].fd = -1;
        lx->clients[i].lx = lx;
        lx->clients[i].num = i;
    }
    lx->fd = -1;

    return lx;
}

void lx200_destroy (struct lx200 *lx)
{
    int i;

    if (lx) {
        for (i = 0; i < MAX_CLIENTS; i++)
            client_free (&lx->clients[i]);
        if (lx->fd != -1)
            close (lx->fd);
        free (lx);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */