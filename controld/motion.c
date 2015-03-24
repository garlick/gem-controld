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

struct motion_struct {
    int fd;
    char *devname;
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
static char *mgets (motion_t m, char *buf, int size)
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

static int mputs (motion_t m, const char *s)
{
    if (m->flags & MOTION_DEBUG) {
        char *cpy = toliteral (s);
        fprintf (stderr, "%s>'%s'\n", m->name, cpy);
        free (cpy);
    }
    return dprintf (m->fd, "%s", s);
}

static int mprintf (motion_t m, const char *fmt, ...)
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
static int mping (motion_t m, int count)
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

static void mdelay (motion_t m, int msec)
{
    if (m->flags & MOTION_DEBUG)
        fprintf (stderr, "%s:delay %dms\n", m->name, msec);
    usleep (1000*msec);
}

/* Send command + \r, receive back echoed command.
 */
static int mcmd (motion_t m, const char *fmt, ...)
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
static int mopen (motion_t m)
{
    struct termios tio;

    if ((m->fd = open(m->devname, O_RDWR | O_NOCTTY)) < 0)
        return -1;
    memset (&tio, 0, sizeof (tio)); 
    tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VTIME] = 0; /* no timeout */
    tio.c_cc[VMIN] = 1;  /* block until 1 char available */
    tcflush (m->fd, TCIFLUSH);
    return tcsetattr(m->fd, TCSANOW, &tio);
}

motion_t motion_init (const char *devname, const char *name, int flags)
{
    motion_t m;
    int saved_errno;
    char buf[max_cmdline];
    int i;

    if (!(m = malloc (sizeof (*m))) || !(m->devname = strdup (devname))
                                    || !(m->name = strdup (name))) {
        errno = ENOMEM;
        goto error;
    }
    m->flags = flags;
    m->fd = -1;
    if (mopen (m) < 0)
        goto error;

    /* Preserve the operating state (avoid setting origin)
     */
    if (m->flags & MOTION_SOFTINIT)
        goto done;

    /* Opening dialog
     */
    if (mputs (m, "\003") < 0)      /* send ctrl-C to reset */
        goto error;
    mdelay (m, 200);                /* wait for device to come out of reset */
    if (mputs (m, " ") < 0)         /* send space to initiate comms */
        goto error;
    for (i = 0; i < 2; i++) {       /* eat 2 lines of init output */
        if (!mgets (m, buf, sizeof (buf)))
            goto error;
    }
    if (m->flags & MOTION_DEBUG) {
        if (mprintf (m, "X\r") < 0) /* eXamine parameters */
            goto error;
        for (i = 0; i < 2; i++) {   /* expect 2 lines (no encoder/auto-pos) */
            if (!mgets (m, buf, sizeof (buf)))
                goto error;
        }
    }
    if (mcmd (m, "A%d", 0x8) < 0)   /* turn off green LED on OUTPUT-1 */
        goto error;
    if (mcmd (m, "M0") < 0)         /* stop any motion */
        goto error;
done:
    if (mping (m, 2) < 0)
        goto error;
    return m;
error:
    saved_errno = errno;
    motion_fini (m);
    errno = saved_errno;
    return NULL;
}

void motion_fini (motion_t m)
{
    if (m) {
        if (m->fd >= 0) {
            (void)mcmd (m, "M0");
            (void)close (m->fd);
        }
        if (m->devname)
            free (m->devname);
        if (m->name)
            free (m->name);
        free (m);
    }
}

const char *motion_name (motion_t m)
{
    return m->name;
}

int motion_set_resolution (motion_t m, int res)
{
    if (res < 0 || res > 8) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "D%d", res);
}

int motion_set_mode (motion_t m, int mode)
{
    if (mode != 0 && mode != 1) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "H%d", mode);
}

int motion_set_current (motion_t m, int hold, int run)
{
    if (hold < 0 || hold > 100 || run < 0 || run > 100) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "Y%d %d", hold, run);
}

int motion_set_acceleration (motion_t m, int accel, int decel)
{
    if (accel < 0 || accel > 255 || decel < 0 || decel > 255) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "K%d %d", accel, decel);
}

int motion_set_velocity (motion_t m, int velocity)
{
    if (velocity != 0 && (abs (velocity) < 20  || abs (velocity) > 20000)) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "M%d", velocity);
}

int motion_get_position (motion_t m, double *position)
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

int motion_set_position (motion_t m, double position)
{
    if (fabs (position) > 8388607.9) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "R%+.2f", position);
}

int motion_get_status (motion_t m, uint8_t *status)
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

int motion_set_index (motion_t m, double offset)
{
    if (fabs (offset) < 0.01 || fabs (offset) > 8388607.99) {
        errno = EINVAL;
        return -1;
    }
    return mcmd (m, "%+.2f", offset);
}

int motion_set_origin (motion_t m)
{
    return mcmd (m, "O");
}

int motion_abort (motion_t m)
{
    return mcmd (m, "@");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
