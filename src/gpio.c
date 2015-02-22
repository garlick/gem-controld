/*****************************************************************************
 *  Copyright (C) 2013 Jim Garlick
 *  Written by Jim Garlick <garlick.jim@gmail.com>
 *  All Rights Reserved.
 *
 *  This file is part of pi-gpio-uinput.
 *  For details, see <https://github.com/garlick/pi-gpio-uinput>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/
/* gpio.c - return a stream of keypress events from pi GPIO */

/* Inputs are configured to interrupt on rising and falling edges.
 * We call poll(2), which blocks until one of the inputs receives a
 * POLLPRI event.  When poll returns, we examine the pollfd array
 * to determine which button changed state and return that as a
 * keypress event to our main program, which feeds that to the uinput
 * driver.
 *
 * Inputs read 0 when button is depressed, 1 when released.
 * We have to record the previous state and, after poll(2) returns
 * indicating it changed, wait DEBOUNCE_MS for the state to settle
 * before reading it and comparing to the previous state.
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

static map_t map[] = {
    { .pin = 66,   .bit = 3,   .fd = -1, },
    { .pin = 67,   .bit = 2,   .fd = -1, },
    { .pin = 68,   .bit = 0,   .fd = -1, },
    { .pin = 69,   .bit = 1,   .fd = -1, },
};
static const int maplen = sizeof (map) / sizeof (map[0]);

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

void gpio_init (void)
{
    int i;

    for (i = 0; i < maplen; i++) {
        _export (map[i].pin);
        _input (map[i].pin);
        _edge (map[i].pin);
        map[i].fd = _open_value (map[i].pin);
        map[i].val = _read_value (map[i].fd);
    }
}

void gpio_fini (void)
{
    int i;
    for (i = 0; i < maplen; i++) {
        if (map[i].fd != -1)
            close (map[i].fd);
            _unexport (map[i].pin);
    }
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

int getword (void)
{
    int i, val = 0;

    for (i = 0; i < maplen; i++) {
        if (map[i].val)
            val |= (1<<map[i].bit);
    }
    return val;
}

int gpio_event (void)
{
    static struct pollfd *pfd = NULL;
    static int lastword = 0xff; /* impossible initial value */
    int timeout = -1;
    struct timespec t0;
    int word = getword ();;
    int i;

    if (pfd == NULL) {
        pfd = malloc (sizeof (pfd[0]) * maplen);
        if (!pfd) {
            fprintf (stderr, "out of memory\n");
            exit(1);
        } 
        memset (pfd, 0, sizeof (pfd[0]) * maplen);
        for (i = 0; i < maplen; i++) {
            pfd[i].fd = map[i].fd;
            pfd[i].events = POLLPRI;
        }
    }
    while (word == lastword) {
        int n = poll (pfd, maplen, timeout);
        if (n < 0) {
            perror ("poll");
            exit (1);
        }
        for (i = 0; i < maplen; i++) {
            if ((pfd[i].revents & POLLPRI))
                map[i].val = _read_value (pfd[i].fd);
        }
        if (n > 0 && timeout == -1) {
            monoclock (&t0);
            timeout = DEBOUNCE_MS;
        }
        if (timeout != -1 && (timeout = DEBOUNCE_MS - monosince (t0)) <= 0) {
            timeout = -1;
            word = getword ();
        }
    }
    lastword = word;
    return word;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
