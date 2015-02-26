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

/* gpio.c - poll a set of gpio lines */

/* Inputs are configured to interrupt on rising and falling edges.
 * gpio_event() blocks until one of the inputs changes state.  It calls
 * poll(2), blocking until one of the inputs receives a POLLPRI event.
 * When poll returns, we examine the pollfd array to determine which input
 * changed state.  If after DEBOUNCE_MS milliseconds there is still a
 * change in state, it is returned as a mask.  The debounce period is set
 * up as a poll timeout, allowing multiple trips through the poll loop during
 * this period without returning anything.
 */


#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#include "gpio.h"

#include "libutil/xzmalloc.h"
#include "libutil/log.h"

#ifndef PATH_MAX
#define PATH_MAX    1024
#endif
#define INTBUFLEN   16

#define DEBOUNCE_MS 5

typedef struct {
    int pin;    /* GPIO pin number */
    int fd;     /* fd open to sysfs 'value' file, passed to poll(2) */
    int bit;    /* bit in output byte */
    int val;    /* current state */
} map_t;

struct gpio_struct {
    map_t *map;
    int maplen;
};

static void _export (int pin)
{
    struct stat sb;
    char path[PATH_MAX];
    char msg[INTBUFLEN];
    FILE *fp;

    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d", pin);
    if (stat (path, &sb) == 0)
        return;
    snprintf (path, sizeof (path), "/sys/class/gpio/export");
    fp = fopen (path, "w");
    if (!fp) {
        perror (path);
        exit (1);
    }
    snprintf (msg, sizeof (msg), "%d", pin);
    if (fputs (msg, fp) < 0) {
        perror (path);
        exit (1);
    }
    if (fclose (fp) != 0) {
        perror (path);
        exit (1);
    }
}

static void _unexport (int pin)
{
    struct stat sb;
    char path[PATH_MAX];
    char msg[INTBUFLEN];
    FILE *fp;

    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d", pin);
    if (stat (path, &sb) < 0)
        return;
    snprintf (path, sizeof (path), "/sys/class/gpio/unexport");
    fp = fopen (path, "w");
    if (!fp) {
        perror (path);
        exit (1);
    }
    snprintf (msg, sizeof (msg), "%d", pin);
    if (fputs (msg, fp) < 0) {
        perror (path);
        exit (1);
    }
    if (fclose (fp) != 0) {
        perror (path);
        exit (1);
    }
}

static void _input (int pin)
{
    char path[PATH_MAX];
    FILE *fp;

    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d/direction", pin);
    fp = fopen (path, "w");
    if (!fp) {
        perror (path);
        exit (1);
    }
    if (fputs ("in", fp) < 0) {
        perror (path);
        exit (1);
    }
    if (fclose (fp) != 0) {
        perror (path);
        exit (1);
    }
}

static void _edge (int pin)
{
    char path[PATH_MAX];
    FILE *fp;

    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d/edge", pin);
    fp = fopen (path, "w");
    if (!fp) {
        perror (path);
        exit (1);
    }
    if (fputs ("both", fp) < 0) {
        perror (path);
        exit (1);
    }
    if (fclose (fp) != 0) {
        perror (path);
        exit (1);
    }
}

static int _read_value (int fd)
{
    char c;

    if (lseek (fd, 0, SEEK_SET) < 0) {
        perror ("lseek");
        exit (0);
    }
    if (read (fd, &c, 1) != 1) {
        perror ("read");
        exit (0);
    }
    return c == '0' ? 0 : 1;
}

static int _open_value (int pin)
{
    char path[PATH_MAX];
    int fd;

    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d/value", pin);
    fd = open (path, O_RDONLY);
    if (fd < 0) {
        perror (path);
        exit (1);
    }
    return fd;
}

gpio_t gpio_init (int *gpio, int len)
{
    gpio_t g = xzmalloc (sizeof (*g));
    int i;

    g->map = xzmalloc (sizeof (map_t)*len);
    g->maplen = len;
    for (i = 0; i < len; i++) {
        g->map[i].pin = gpio[i];
        g->map[i].bit = i;
        _export (g->map[i].pin);
        _input (g->map[i].pin);
        _edge (g->map[i].pin);
        g->map[i].fd = _open_value (g->map[i].pin);
        g->map[i].val = _read_value (g->map[i].fd);
    }
    return g;
}

void gpio_fini (gpio_t g)
{
    int i;
    for (i = 0; i < g->maplen; i++) {
        if (g->map[i].fd != -1)
            close (g->map[i].fd);
        _unexport (g->map[i].pin);
    }
    free (g->map);
    free (g);
}

void monoclock (struct timespec *ts)
{
    if (clock_gettime (CLOCK_MONOTONIC, ts) < 0) {
        perror ("clock_gettime");
        exit (1);
    }
}

int monosince (struct timespec t0)
{
    struct timespec t1;
    monoclock (&t1);
    return (t1.tv_sec * 1E3 + t1.tv_nsec * 1E-6)
         - (t0.tv_sec * 1E3 + t0.tv_nsec * 1E-6);
}

int getword (gpio_t g)
{
    int i, val = 0;

    for (i = 0; i < g->maplen; i++) {
        if (g->map[i].val)
            val |= (1<<g->map[i].bit);
    }
    return val;
}

int gpio_event (gpio_t g)
{
    static struct pollfd *pfd = NULL;
    static int lastword = 0xff; /* impossible initial value */
    int timeout = -1;
    struct timespec t0;
    int word = getword (g);
    int i;

    if (pfd == NULL) {
        pfd = malloc (sizeof (pfd[0]) * g->maplen);
        if (!pfd) {
            fprintf (stderr, "out of memory\n");
            exit(1);
        } 
        memset (pfd, 0, sizeof (pfd[0]) * g->maplen);
        for (i = 0; i < g->maplen; i++) {
            pfd[i].fd = g->map[i].fd;
            pfd[i].events = POLLPRI;
        }
    }
    while (word == lastword) {
        int n = poll (pfd, g->maplen, timeout);
        if (n < 0) {
            perror ("poll");
            exit (1);
        }
        for (i = 0; i < g->maplen; i++) {
            if ((pfd[i].revents & POLLPRI))
                g->map[i].val = _read_value (pfd[i].fd);
        }
        if (n > 0 && timeout == -1) {
            monoclock (&t0);
            timeout = DEBOUNCE_MS;
        }
        if (timeout != -1) {
            timeout = DEBOUNCE_MS - monosince (t0);
            if (timeout <= 0) { /* expired */
                timeout = -1;
                word = getword (g);
            }
        }
    }
    lastword = word;
    return word;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
