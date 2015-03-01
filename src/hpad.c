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

/* libev doesn't grok POLL_PRI so we use epoll to create a
 * single file descriptor that becomes readable when any fd's
 * it is watching have the POLL_PRI event.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <libgen.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ev.h>

#include "libutil/log.h"
#include "libutil/xzmalloc.h"
#include "libutil/gpio.h"

#include "hpad.h"

typedef struct {
    int pin;
    int fd;
    struct epoll_event e;
} pin_t;

struct hpad_struct {
    pin_t pins[4];
    int efd;
    hpad_cb_t cb;
    void *cb_arg;
    int debounce_msec;
    ev_io w;
};

hpad_t hpad_new (void)
{
    hpad_t h = xzmalloc (sizeof (*h));
    int i;
    for (i = 0; i < 4; i++)
        h->pins[i].fd = -1;
    h->efd = -1;
    return h;
}

void hpad_destroy (hpad_t h)
{
    int i;
    if (h) {
        if (h->efd != -1)
            close (h->efd);
        for (i = 0; i < 4; i++) {
            pin_t *p = &h->pins[i];
            if (p->fd != -1) {
                (void)close (p->fd);
                (void)gpio_set_export (p->pin, false);
            }
        }
        free (h);
    }
}

static void gpio_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    msg ("%s", __FUNCTION__);
}

int hpad_init (hpad_t h, const char *pins, int debounce_msec,
               hpad_cb_t cb, void *arg)
{
    int i, rc = -1;
    char *tok, *cpy = NULL;

    if (!pins) {
        errno = EINVAL;
        goto done;
    }
    if ((h->efd = epoll_create (4)) < 0)
        goto done;
    cpy = xstrdup (pins);
    tok = strtok (cpy, ",");
    for (i = 0; i < 4; i++) {
        pin_t *p = &h->pins[i];
        if (!tok) {
            errno = EINVAL;
            goto done;
        } 
        p->pin = strtoul (tok, NULL, 10);
        if (gpio_set_export (p->pin, true) < 0)
            goto done;
        if (gpio_set_direction (p->pin, "in") < 0)
            goto done;
        if (gpio_set_edge (p->pin, "both") < 0)
            goto done;
        if ((p->fd = gpio_open (p->pin, O_RDONLY)) < 0)
            goto done;
        p->e.data.fd = p->fd;
        p->e.events = POLL_PRI;
        if (epoll_ctl (h->efd, EPOLL_CTL_ADD, p->fd, &p->e) < 0)
            goto done;
        tok = strtok (NULL, ",");
    }
    h->debounce_msec = debounce_msec;
    h->cb = cb;
    h->cb_arg = arg;
    ev_io_init (&h->w, gpio_cb, h->efd, EV_READ);
    rc = 0;
done:
    if (cpy)
        free (cpy);
    return rc;
}

void hpad_start (struct ev_loop *loop, hpad_t h)
{
    ev_io_start (loop, &h->w);
}

void hpad_stop (struct ev_loop *loop, hpad_t h)
{
    ev_io_stop (loop, &h->w);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
