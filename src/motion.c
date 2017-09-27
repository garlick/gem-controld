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

/* motion.c - communicate with Intelligent Motion Devices IM483I indexer */

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <math.h>

#include "motion.h"

struct motion {
    int fd;
    char *name;
    int flags;
};

const int max_cmdline = 80;

/* Translate funny characters into readable debug output.
 * Caller must free resulting string.
 */
static char *toliteral (const char *s)
{
    int len = strlen (s) * 4 + 1;
    char *buf;
    const char *p = s;

    if ((buf = malloc (len))) {
        memset (buf, 0, len);
        while (*p) {
            if (*p == '\r')
                sprintf (buf + strlen (buf), "\\r");
            else if (*p < ' ' || *p > '~')
                sprintf (buf + strlen (buf), "\\%.3o", *p);
            else
                buf[strlen (buf)] = *p;
            p++;
        }
    }
    return buf;
}

static int dgetc (int fd)
{
    uint8_t c;
    int n = read (fd, &c, 1);
    if (n == 0)
        errno = ETIMEDOUT;
    if (n <= 0)
        return -1;
    return c;
}

/* Read a line ending in \r\n from 'fd' and return it, without the \r\n,
 * using buf[size] as storage.  If there is a read error or buf overflow,
 * return NULL.
 */
static char *mgets (struct motion *m, char *buf, int size)
{
    int c;
    int i = 0;
    int term = 0;

    assert (size > 1);
    memset (buf, 0, size);
    while (term < 2 && i < size - 2 && (c = dgetc (m->fd)) != -1) {
        if (c == '\r' || c == '\n')
            term++;
        else {
            term = 0;
            buf[i++] = c;
        }
    }
    if (term != 2)
        return NULL;
    buf[i] = '\0';
    if (m->flags & MOTION_DEBUG) {
        char *cpy = toliteral (buf);
        fprintf (stderr, "%s<'%s'\n", m->name, cpy);
        free (cpy);
    }
    return buf;
}

static int mputs (struct motion *m, const char *s)
{
    if (m->flags & MOTION_DEBUG) {
        char *cpy = toliteral (s);
        fprintf (stderr, "%s>'%s'\n", m->name, cpy);
        free (cpy);
    }
    return dprintf (m->fd, "%s", s);
}

static int mprintf (struct motion *m, const char *fmt, ...)
{
    va_list ap;
    char *s = NULL;
    int rc;

    va_start (ap, fmt);
    if (vasprintf (&s, fmt, ap) < 0)
        return -1;
    va_end (ap);
    rc = mputs (m, s);
    free (s);
    return rc;
}

/* Send a \r, receive a #.
 */
static int mping (struct motion *m, int count)
{
    int i;
    char buf[max_cmdline];
    int rc = -1;

    for (i = 0; i < count; i++) {
        if (mputs (m, "\r") < 0 || !mgets (m, buf, sizeof (buf)))
            goto done;
        if (strcmp (buf, "#") != 0) {
            errno = EPROTO;
            goto done;
        }
    }
    rc = 0;
done:
    return rc;
}

static void mdelay (struct motion *m, int msec)
{
    if (m->flags & MOTION_DEBUG)
        fprintf (stderr, "%s:delay %dms\n", m->name, msec);
    usleep (1000*msec);
}

/* Send command + \r, receive back echoed command.
 */
static int mcmd (struct motion *m, const char *fmt, ...)
{
    va_list ap;
    int rc = -1;
    char *cmd = NULL;
    char buf[max_cmdline];

    va_start (ap, fmt);
    if (vasprintf (&cmd, fmt, ap) < 0)
        return -1;
    va_end (ap);
    if (mprintf (m, "%s\r", cmd) < 0)
        goto done;
    if (!mgets (m, buf, sizeof (buf)))
        goto done;
    if (strcmp (buf, cmd) != 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    if (cmd)
        free (cmd);
    return rc;
}

/* Open/configure the serial port
 */
static int mopen (const char *devname)
{
    struct termios tio;
    int fd;

    if ((fd = open (devname, O_RDWR | O_NOCTTY)) < 0)
        return -1;
    memset (&tio, 0, sizeof (tio));
    tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VTIME] = 0; /* no timeout */
    tio.c_cc[VMIN] = 1;  /* block until 1 char available */
    tcflush (fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        close (fd);
        return -1;
    }
    return fd;
}

/* Cold start: send " \r", read 2 banner lines and "#".
 * Warm start: send " \r", read " #".
 */
static int mhello (struct motion *m, bool *coldstart)
{
    char buf[max_cmdline];
    int i;
    bool warm = false;

    if (mputs (m, " \r") < 0)
        return -1;
    for (i = 0; i < 3; i++) {
        if (!mgets (m, buf, sizeof (buf)))
            return -1;
        if (!strcmp (buf, " #")) {
            warm = true;
            break;
        }
    }
    if (coldstart)
        *coldstart = !warm;
    return 0;
}

/* Send ctrl-C to reset device, then delay while it comes out of reset.
 */
static int mreset (struct motion *m)
{
    if (mputs (m, "\003") < 0)
        return -1;
    mdelay (m, 200);
    return 0;
}

/* Examine configuration parameters (via debug output).
 * Assume two lines are returned (three if encoder option installed).
 */
static int mexamine (struct motion *m)
{
    char buf[max_cmdline];
    int i;

    if (mprintf (m, "X\r") < 0)
        return -1;
    for (i = 0; i < 2; i++) {
        if (!mgets (m, buf, sizeof (buf)))
            return -1;
    }
    return 0;
}


int motion_set_resolution (struct motion *m, int res)
{
    if (res < 0 || res > 8) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "D%d", res);
}

int motion_set_mode (struct motion *m, int mode)
{
    if (mode != 0 && mode != 1) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "H%d", mode);
}

int motion_set_current (struct motion *m, int hold, int run)
{
    if (hold < 0 || hold > 100 || run < 0 || run > 100) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "Y%d %d", hold, run);
}

int motion_set_acceleration (struct motion *m, int accel, int decel)
{
    if (accel < 0 || accel > 255 || decel < 0 || decel > 255) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "K%d %d", accel, decel);
}

int motion_set_initial_velocity (struct motion *m, int velocity)
{
    if (velocity < 20  || velocity > 20000) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "I%d", velocity);
}

int motion_set_final_velocity (struct motion *m, int velocity)
{
    if (velocity < 20  || velocity > 20000) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "V%d", velocity);
}

int motion_set_velocity (struct motion *m, int velocity)
{
    if (velocity != 0 && (abs (velocity) < 20  || abs (velocity) > 20000)) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "M%d", velocity);
}

int motion_get_position (struct motion *m, double *position)
{
    char buf[max_cmdline];
    float pos;

    if (mprintf (m, "Z0\r") < 0)
        return -1;
    if (!mgets (m, buf, sizeof (buf)))
        return -1;
    if (sscanf (buf, "Z0 %f", &pos) != 1)
        return -1;
    *position = (double)pos;
    return 0;
}

int motion_set_position (struct motion *m, double position)
{
    if (fabs (position) > 8388607.9) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "R%+.2f", position);
}

int motion_get_status (struct motion *m, uint8_t *status)
{
    char buf[max_cmdline];
    int s;

    if (mprintf (m, "^\r") < 0)
        return -1;
    if (!mgets (m, buf, sizeof (buf)))
        return -1;
    if (sscanf (buf, "^ %d", &s) != 1)
        return -1;
    *status  = s;
    return 0;
}

int motion_set_index (struct motion *m, double offset)
{
    if (fabs (offset) < 0.01 || fabs (offset) > 8388607.99) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "%+.2f", offset);
}

int motion_set_origin (struct motion *m)
{
    return mcmd (m, "O");
}

int motion_stop (struct motion *m)
{
    return mcmd (m, "@");
}

int motion_set_io (struct motion *m, uint8_t val)
{
    return mcmd (m, "A%d", val);
}

int motion_get_io (struct motion *m, uint8_t *val)
{
    char buf[max_cmdline];
    int v;

    if (mprintf (m, "A129\r") < 0)
        return -1;
    if (!mgets (m, buf, sizeof (buf)))
        return -1;
    if (sscanf (buf, "A129 %d", &v) != 1)
        return -1;
    *val = v;
    return 0;
}

int motion_init (struct motion *m, const char *devname, int flags,
                 bool *coldstart)
{
    if (m->fd != -1) {
        errno = EINVAL;
        goto error;
    }
    if ((m->fd = mopen (devname)) < 0)
        goto error;
    m->flags = flags;
    if ((m->flags & MOTION_RESET)) {
        if (mreset (m) < 0)
            goto error;
    }
    if (mhello (m, coldstart) < 0)
        goto error;
    if (m->flags & MOTION_DEBUG) {
        if (mexamine (m) < 0)
            goto error;
    }
    if (mping (m, 2) < 0)
        goto error;
    return 0;
error:
    return -1;
}

const char *motion_get_name (struct motion *m)
{
    return m->name;
}

void motion_destroy (struct motion *m)
{
    if (m) {
        if (m->fd >= 0)
            (void)close (m->fd);
        free (m->name);
        free (m);
    }
}

struct motion *motion_new (const char *name)
{
    struct motion *m;
    if (!(m = calloc (1, sizeof (*m))))
        goto error;
    m->fd = -1;
    if (!(m->name = strdup (name)))
        goto error;
    return m;
error:
    motion_destroy (m);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
