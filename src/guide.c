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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <libgen.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ev.h>

#include "log.h"
#include "xzmalloc.h"
#include "gpio.h"
#include "slew.h"

#include "guide.h"

struct pin {
    int pin;
    int fd;
    struct epoll_event e;
};

struct guide {
    int flags;
    struct pin pins[4];
    int efd;
    guide_cb_t cb;
    void *cb_arg;
    double debounce;
    int val;
    ev_io io_w;
    ev_timer timer_w;
};

struct guide *guide_new (void)
{
    struct guide *g = xzmalloc (sizeof (*g));
    int i;
    for (i = 0; i < 4; i++)
        g->pins[i].fd = -1;
    g->efd = -1;
    return g;
}

void guide_destroy (struct guide *g)
{
    int i;
    if (g) {
        if (g->efd != -1)
            close (g->efd);
        for (i = 0; i < 4; i++) {
            struct pin *p = &g->pins[i];
            if (p->fd != -1) {
                (void)close (p->fd);
                (void)gpio_set_export (p->pin, false);
            }
        }
        free (g);
    }
}

static void guide_dump (int val)
{
    msg ("guide: (0x%x) %sRA+ %sRA- %sDEC+ %sDEC-", val,
         (val & SLEW_RA_PLUS) ? "*" : " ",
         (val & SLEW_RA_MINUS) ? "*" : " ",
         (val & SLEW_DEC_PLUS) ? "*" : " ",
         (val & SLEW_DEC_MINUS) ? "*" : " ");
}

/* N.B. guide pins configuration requires pins to be listed in the
 * same order as slew.h direction bits.
 */
int guide_get_slew_direction (struct guide *g)
{
    int code = 0;
    int i;

    for (i = 0; i < 4; i++) {
        struct pin *p = &g->pins[i];
        int val;
        if (gpio_read (p->fd, &val) < 0)
            return -1;
        code |= (val<<i);
    }
    if ((g->flags & GUIDE_DEBUG))
        guide_dump (code);
    return code;
}

static void timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    struct guide *g = (struct guide *)((char *)w
                    - offsetof (struct guide, timer_w));
    int val = guide_get_slew_direction (g);
    if (val != g->val) {
        g->val = val;
        g->cb (g, g->cb_arg);
    }
}

static void gpio_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    struct guide *g = (struct guide *)((char *)w
                    - offsetof (struct guide, io_w));
    if (!ev_is_active (&g->timer_w)) {
        ev_timer_set (&g->timer_w, g->debounce, 0.);
        ev_timer_start (loop, &g->timer_w);
    }
}

int guide_init (struct guide *g, const char *pins, double debounce,
                guide_cb_t cb, void *arg, int flags)
{
    int i, rc = -1;
    char *tok, *cpy = NULL;

    g->flags = flags;
    if (!pins) {
        errno = EINVAL;
        goto done;
    }
    if ((g->efd = epoll_create (4)) < 0)    /* need this because libev */
        goto done;                          /*   doesn't grok POLLPRI */
    cpy = xstrdup (pins);
    tok = strtok (cpy, ",");
    for (i = 0; i < 4; i++) {
        struct pin *p = &g->pins[i];
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
        if (gpio_set_polarity (p->pin, false) < 0) // active low
            goto done;
        if ((p->fd = gpio_open (p->pin, O_RDONLY)) < 0)
            goto done;
        p->e.data.fd = p->fd;
        p->e.events = EPOLLPRI;
        if (epoll_ctl (g->efd, EPOLL_CTL_ADD, p->fd, &p->e) < 0)
            goto done;
        tok = strtok (NULL, ",");
        if ((g->flags & GUIDE_DEBUG))
            msg ("%s: configured gpio %d", __FUNCTION__, p->pin);
    }
    g->debounce = debounce;
    g->cb = cb;
    g->cb_arg = arg;
    ev_io_init (&g->io_w, gpio_cb, g->efd, EV_READ);
    ev_timer_init (&g->timer_w, timer_cb, g->debounce, 0.);
    g->val = guide_get_slew_direction (g);
    rc = 0;
done:
    if (cpy)
        free (cpy);
    return rc;
}

void guide_start (struct ev_loop *loop, struct guide *g)
{
    ev_io_start (loop, &g->io_w);
}

void guide_stop (struct ev_loop *loop, struct guide *g)
{
    ev_io_stop (loop, &g->io_w);
    ev_timer_stop (loop, &g->timer_w);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
