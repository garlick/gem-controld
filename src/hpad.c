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

#include "hpad.h"

/* Bartels stepper controller handpad returns these values
 * when keys are pressed.
 */
enum {
    HPAD_KEY_NORTH = 1,
    HPAD_KEY_SOUTH = 2,
    HPAD_KEY_WEST = 3,
    HPAD_KEY_EAST = 4,
    HPAD_KEY_M1 = 5,
    HPAD_KEY_M2 = 6,
    HPAD_KEY_FAST = 8
};

struct pin {
    int pin;
    int fd;
    struct epoll_event e;
};

struct hpad {
    struct pin pins[4];
    int efd;
    hpad_cb_t cb;
    void *cb_arg;
    double debounce;
    int val;
    ev_io io_w;
    ev_timer timer_w;
    int flags;
};

struct hpad *hpad_new (void)
{
    struct hpad *h = xzmalloc (sizeof (*h));
    int i;
    for (i = 0; i < 4; i++)
        h->pins[i].fd = -1;
    h->efd = -1;
    return h;
}

void hpad_destroy (struct hpad *h)
{
    int i;
    if (h) {
        if (h->efd != -1)
            close (h->efd);
        for (i = 0; i < 4; i++) {
            struct pin *p = &h->pins[i];
            if (p->fd != -1) {
                (void)close (p->fd);
                (void)gpio_set_export (p->pin, false);
            }
        }
        free (h);
    }
}

static int hpad_read (struct hpad *h)
{
    int code = 0;
    int i;

    for (i = 0; i < 4; i++) {
        struct pin *p = &h->pins[i];
        int val;
        if (gpio_read (p->fd, &val) < 0)
            return -1;
        code |= (val<<i);
    }
    return code;
}

static void hpad_dump_slew_direction (int val)
{
    msg ("hpad: (0x%x) %sRA+ %sRA- %sDEC+ %sDEC-", val,
         (val & SLEW_RA_PLUS) ? "*" : " ",
         (val & SLEW_RA_MINUS) ? "*" : " ",
         (val & SLEW_DEC_PLUS) ? "*" : " ",
         (val & SLEW_DEC_MINUS) ? "*" : " ");
}

int hpad_get_slew_direction (struct hpad *h)
{
    int result = 0;

    switch (h->val & 0x7) {
        case (HPAD_KEY_M1 | HPAD_KEY_M2):
        case HPAD_KEY_M1:
        case HPAD_KEY_M2:
            break;
        case HPAD_KEY_WEST:
            result = SLEW_RA_MINUS;
            break;
        case HPAD_KEY_NORTH:
            result = SLEW_DEC_PLUS;
            break;
        case HPAD_KEY_SOUTH:
            result = SLEW_DEC_MINUS;
            break;
        case HPAD_KEY_EAST:
            result = SLEW_RA_PLUS;
            break;
    }
    if ((h->flags & HPAD_DEBUG))
        hpad_dump_slew_direction (result);
    return result;
}

int hpad_get_slew_rate (struct hpad *h)
{
    int result;

    if ((h->val & HPAD_KEY_FAST)) {
        if ((h->flags & HPAD_DEBUG))
            msg ("hpad: rate=fast");
        result = SLEW_RATE_FAST;
    }
    else {
        if ((h->flags & HPAD_DEBUG))
            msg ("hpad: rate=slow");
        result = SLEW_RATE_MEDIUM;
    }
    return result;
}

int hpad_get_control (struct hpad *h)
{
    int result = 0;

    switch ((h->val & 0x7)) {
        case (HPAD_KEY_M1 | HPAD_KEY_M2):
            if ((h->flags & HPAD_DEBUG))
                msg ("hpad: control=M1+M2");
            result = HPAD_CONTROL_M1 | HPAD_CONTROL_M2;
            break;
        case HPAD_KEY_M1:
            if ((h->flags & HPAD_DEBUG))
                msg ("hpad: control=M1");
            result = HPAD_CONTROL_M1;
            break;
        case HPAD_KEY_M2:
            if ((h->flags & HPAD_DEBUG))
                msg ("hpad: control=M2");
            result = HPAD_CONTROL_M2;
            break;
    }
    return result;
}

static void timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    struct hpad *h = (struct hpad *)((char *)w
                    - offsetof (struct hpad, timer_w));
    int val = hpad_read (h);
    if (val != h->val) {
        h->val = val;
        h->cb (h, h->cb_arg);
    }
}

static void gpio_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    struct hpad *h = (struct hpad *)((char *)w
                    - offsetof (struct hpad, io_w));
    if (!ev_is_active (&h->timer_w)) {
        ev_timer_set (&h->timer_w, h->debounce, 0.);
        ev_timer_start (loop, &h->timer_w);
    }
}

int hpad_init (struct hpad *h, const char *pins, double debounce,
               hpad_cb_t cb, void *arg, int flags)
{
    int i, rc = -1;
    char *tok, *cpy = NULL;

    if (!pins) {
        errno = EINVAL;
        goto done;
    }
    h->flags = flags;
    if ((h->efd = epoll_create (4)) < 0)    /* need this because libev */
        goto done;                          /*   doesn't grok POLLPRI */
    cpy = xstrdup (pins);
    tok = strtok (cpy, ",");
    for (i = 0; i < 4; i++) {
        struct pin *p = &h->pins[i];
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
        p->e.events = EPOLLPRI;
        if (epoll_ctl (h->efd, EPOLL_CTL_ADD, p->fd, &p->e) < 0)
            goto done;
        tok = strtok (NULL, ",");
        if ((h->flags & HPAD_DEBUG))
            msg ("%s: configured gpio %d", __FUNCTION__, p->pin);
    }
    h->debounce = debounce;
    h->cb = cb;
    h->cb_arg = arg;
    ev_io_init (&h->io_w, gpio_cb, h->efd, EV_READ);
    ev_timer_init (&h->timer_w, timer_cb, h->debounce, 0.);
    h->val = hpad_read (h);
    rc = 0;
done:
    if (cpy)
        free (cpy);
    return rc;
}

void hpad_start (struct ev_loop *loop, struct hpad *h)
{
    ev_io_start (loop, &h->io_w);
}

void hpad_stop (struct ev_loop *loop, struct hpad *h)
{
    ev_io_stop (loop, &h->io_w);
    ev_timer_stop (loop, &h->timer_w);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
