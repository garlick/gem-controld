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

/* motion.c - communicate with IMS im483i indexer */

/* N.B. The im483i can only handle one command a time.  It is "busy" if a
 * command has been sent, but a result has not yet been received.
 *
 * Commands are terminated with \r.
 * Results are terminated with \r\n.
 *
 * Communication is assumed to be in single mode, not party line.
 *
 * The im483ie (encoder version) should work but no support for encoder
 * based operations is included.
 *
 * The Z1 mode, which causes position updates terminated with \r to be sent
 * continuously until the next command, is not used here, and is not handled
 * by the command/result framing code.
 *
 * Ref: High Performance Microstepper Driver & Indexer Software Reference
 * Manual, Intelligent Motion Systems, Inc.
 * https://motion.schneider-electric.com/downloads/manuals/imx_software.pdf
 * http://lennon.astro.northwestern.edu/Computers/IMSmanual.html
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <math.h>
#include <ev.h>

#include "log.h"
#include "motion.h"

#define MAX_CMD     80
#define MAX_BUF     1024


struct motion {
    int fd;
    char *name;
    int flags;
    ev_io io_w;
    ev_timer status_poll_w;
    ev_timer timeout_w;
    bool timeout;
    struct ev_loop *main_loop;
    struct ev_loop *tmp_loop;
    char inbuf[MAX_BUF];
    int inbuf_len;
    bool busy;
    motion_cb_f cb;
    void *cb_arg;
    struct motion_config cfg;
};

static const double status_poll_sec = 0.3;  // poll period during goto

static const double timeout_sec = 5.;   // waiting for result - give up
static const double warn_sec = 1.;      // waiting for result - warn

static int result_recv (struct motion *m, char *buf, int len);
static void result_clear (struct motion *m);
static int serial_send (int fd, const char *s);


/* Translate unprintable characters into readable debug output.
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
            else if (*p == '\n')
                sprintf (buf + strlen (buf), "\\n");
            else if (*p < ' ' || *p > '~')
                sprintf (buf + strlen (buf), "\\%.3o", *p);
            else
                buf[strlen (buf)] = *p;
            p++;
        }
    }
    return buf;
}

/* If busy, wait for result, then clear result buffer.
 * Send string to serial port with \r terminator, then set busy flag.
 */
static int command_send (struct motion *m, const char *s)
{
    char buf[MAX_CMD];

    if (m->busy) {
        if (result_recv (m, NULL, 0) < 0)
            return -1;
    }
    result_clear (m);

    if (snprintf (buf, sizeof (buf), "%s\r", s) >= sizeof (buf)) {
        errno = EINVAL;
        return -1;
    }
    if (m->flags & MOTION_DEBUG) {
        char *cpy = toliteral (buf);
        fprintf (stderr, "%s>'%s'\n", m->name, cpy);
        free (cpy);
    }
    if (serial_send (m->fd, buf) < 0)
        return -1;

    m->busy = true;

    return 0;
}

/* printf-style wrapper for command_send().
 */
static int command_sendf (struct motion *m, const char *fmt, ...)
{
    va_list ap;
    char *s = NULL;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&s, fmt, ap);
    va_end (ap);
    if (rc < 0)
        goto done;
    rc = command_send (m, s);
done:
    free (s);
    return rc;
}

/* helper for result_clear() and result_recv() */
static int result_consume (struct motion *m, char *buf, int len)
{
    char *p;
    int used;

    if (!(p = strstr (m->inbuf, "\r\n")))
        return -1;
    *p = '\0';

    if ((m->flags & MOTION_DEBUG)) {
        char *cpy = toliteral (m->inbuf);
        fprintf (stderr, "%s<'%s\\r\\n'\n", m->name, cpy);
        free (cpy);
    }

    if (buf)
        snprintf (buf, len, "%s", m->inbuf);

    used = strlen (m->inbuf) + 2;
    memmove (m->inbuf, m->inbuf + used, m->inbuf_len - used);
    m->inbuf_len -= used;
    m->inbuf[m->inbuf_len] = '\0';
    m->busy = false;
    return 0;
}

/* Clear the result buffer.
 */
static void result_clear (struct motion *m)
{
    while (result_consume (m, NULL, 0) == 0)
        ;
}

static void timeout_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    struct motion *m = (struct motion *)((char *)w
                        - offsetof (struct motion, timeout_w));

    ev_break (loop, EVBREAK_ALL);
    m->timeout = true;
}

/* Block until one or more results are received and stored in the inbuf,
 * then unwrap one and return it in 'buf' without the \r\n termination.
 */
static int result_recv (struct motion *m, char *buf, int len)
{
    double t0, wait_time = 0.;

    if (!strstr (m->inbuf, "\r\n")) {
        /* move io watcher to temporary loop */
        if (m->main_loop)
            ev_io_stop (m->main_loop, &m->io_w);
        ev_io_start (m->tmp_loop, &m->io_w);

        m->timeout = false;
        t0 = ev_now (m->tmp_loop);
        ev_timer_set (&m->timeout_w, timeout_sec, 0.);
        ev_timer_start (m->tmp_loop, &m->timeout_w);
        while (!strstr (m->inbuf, "\r\n")) {
            if (ev_run (m->tmp_loop, EVRUN_ONCE) < 0 || m->timeout)
                break;
        }
        ev_timer_stop (m->tmp_loop, &m->timeout_w);
        wait_time = ev_now (m->tmp_loop) - t0;

        /* move io watcher back to main loop */
        ev_io_stop (m->tmp_loop, &m->io_w);
        if (m->main_loop)
            ev_io_start (m->main_loop, &m->io_w);
    }
    if (result_consume (m, buf, len) < 0) {
        if (m->timeout)
            errno = ETIMEDOUT;
        else
            errno = EIO;
        return -1;
    }
    if (wait_time > warn_sec)
        msg ("%s: waited %.1lfs for result '%s'", m->name, wait_time, buf);
    return 0;
}

/* Handle EV_READ event on im483i serial port.
 */
static void serial_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    struct motion *m = (struct motion *)((char *)w
                        - offsetof (struct motion, io_w));
    int n;

    if ((revents & EV_READ)) {
        do {
            n = read (m->fd, m->inbuf + m->inbuf_len,
                      sizeof (m->inbuf) - m->inbuf_len - 1);
            if (n > 0) {
                m->inbuf_len += n;
                m->inbuf[m->inbuf_len] = '\0';
            }
            else if (n < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                    err ("%s: read", m->name);
            }
        } while (n > 0);
    }
}

/* Send string to im483i serial port in its entirety.
 */
static int serial_send (int fd, const char *s)
{
    int len = strlen (s);
    int sent = 0;
    int n;

    while (sent < len) {
        n = write (fd, s + sent, len - sent);
        if (n > 0) {
            sent += n;
        }
        else if (n < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                return -1;
        }
    }
    return sent;
}

/* Open/configure the serial port for non-blocking I/O,
 * hardwiring parameters needed for the im483i.
 */
static int serial_open (const char *devname)
{
    struct termios tio;
    int fd;

    if ((fd = open (devname, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
        return -1;
    memset (&tio, 0, sizeof (tio));
    tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VTIME] = 0; /* no timeout */
    tio.c_cc[VMIN] = 1;  /* not ready unless at least one character */
    tcflush (fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        close (fd);
        return -1;
    }
    return fd;
}

/* ^C - software reset
 * Returns im483i to power-up state.
 */
static int motion_reset (struct motion *m)
{
    char buf[MAX_CMD];

    if (m->flags & MOTION_DEBUG) {
        fprintf (stderr, "%s>'\\003' + 200ms delay\n", m->name);
    }
    if (serial_send (m->fd, "\003") < 0)
        return -1;
    usleep (1000*200);                // wait for hardware

    m->busy = false;
    m->inbuf[0] = '\0';
    m->inbuf_len = 0;

    if (command_send (m, " ") < 0)
        return -1;
    for (;;) {
        if (result_recv (m, buf, sizeof (buf)) < 0)
            return -1;
        if (!strcmp (buf, "#"))
            break;
    }
    return 0;
}

/* M - move at fixed velocity
 * Motion may be terminated by @-soft stop, M0-velocity zero, or ESC-abort.
 * N.B. motion does not resume automatically after an index command.
 */
int motion_move_constant (struct motion *m, int sps)
{
    if (m->cfg.ccw)
        sps *= -1;
    if (sps != 0 && (abs (sps) < 20  || abs (sps) > 20000)) {
        errno = EINVAL;
        return -1;
    }
    return command_sendf (m, "M%d", sps);
}

/* Z - read position (non encoder).
 */
int motion_get_position (struct motion *m, double *position)
{
    char buf[MAX_CMD];
    double pos;

    if (command_sendf (m, "Z0") < 0)
        return -1;
    if (result_recv (m, buf, sizeof (buf)) < 0)
        return -1;
    if (sscanf (buf, "Z0 %lf", &pos) != 1) {
        errno = EPROTO;
        return -1;
    }
    *position = pos * (m->cfg.ccw ? -1 : 1);
    return 0;
}

/* ^ - read moving status
 */
int motion_get_status (struct motion *m, int *status)
{
    char buf[MAX_CMD];
    int s;

    if (command_sendf (m, "^") < 0)
        return -1;
    if (result_recv (m, buf, sizeof (buf)) < 0)
        return -1;
    if (sscanf (buf, "^ %d", &s) != 1) {
        errno = EPROTO;
        return -1;
    }
    *status = s;
    return 0;
}

/* R - relative index
 */
int motion_goto_absolute (struct motion *m, double position)
{
    if (m->cfg.ccw)
        position *= -1;
    if (fabs (position) > 8388607.9) {
        errno = EINVAL;
        return -1;
    }
    if (command_sendf (m, "R%+.2f", position) < 0)
        return -1;
    ev_timer_set (&m->status_poll_w, status_poll_sec, status_poll_sec);
    ev_timer_start (m->main_loop, &m->status_poll_w);
    return 0;
}

/* +/- - index
 */
int motion_goto_relative (struct motion *m, double offset)
{
    if (m->cfg.ccw)
        offset *= -1;
    if (fabs (offset) < 0.01 || fabs (offset) > 8388607.99) {
        errno = EINVAL;
        return -1;
    }
    if (command_sendf (m, "%+.2f", offset) < 0)
        return -1;
    ev_timer_set (&m->status_poll_w, status_poll_sec, status_poll_sec);
    ev_timer_start (m->main_loop, &m->status_poll_w);
    return 0;
}

/* O - set origin
 */
int motion_set_origin (struct motion *m)
{
    return command_send (m, "O");
}

/* @ - soft stop
 */
int motion_soft_stop (struct motion *m)
{
    return command_send (m, "@");
}

/* ESC - abort
 */
int motion_abort (struct motion *m)
{
    m->busy = false; // don't wait for command result
    return command_send (m, "\033");
}

/* A - port write
 */
int motion_set_io (struct motion *m, uint8_t val)
{
    return command_sendf (m, "A%d", val);
}

/* A - port read
 */
int motion_get_io (struct motion *m, uint8_t *val)
{
    char buf[MAX_CMD];
    int value;

    if (command_send (m, "A129") < 0)
        return -1;
    if (result_recv (m, buf, sizeof (buf)) < 0)
        return -1;
    if (sscanf (buf, "A129 %d", &value) != 1) {
        errno = EPROTO;
        return -1;
    }
    *val = (uint8_t)value;
    return 0;
}

/* Poll for moving status change during a goto.
 * FIXME: estimate of time needed for goto could reduce polling overhead.
 */
static void status_poll_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    struct motion *m = (struct motion *)((char *)w
                        - offsetof (struct motion, status_poll_w));
    int status;

    if (motion_get_status (m, &status) < 0) {
        err ("motion_get_status");
        return;
    }
    if (!(status & MOTION_STATUS_MOVING)) {
        ev_timer_stop (loop, w);
        if (m->cb)
            m->cb (m, m->cb_arg);
    }
}

/* Calculate velocity in steps/sec for motion controller from degrees/sec.
 * Take into account controller velocity scaling in 'auto' mode.
 * Then move at that velocity.
 */
int motion_move_constant_dps (struct motion *m, double dps)
{
    double sps = dps * m->cfg.steps / 360.;

    if (m->cfg.mode == 1) // fixed=0, auto=1
        sps *= 1<<(m->cfg.resolution);
    return motion_move_constant (m, lrint (sps));
};

static int motion_configure (struct motion *m, struct motion_config *cfg)
{
    if (cfg->resolution < 0 || cfg->resolution > 8)
        goto inval;
    if (command_sendf (m, "D%d", cfg->resolution) < 0)
        goto error;
    if (cfg->mode != 0 && cfg->mode != 1)
        goto inval;
    if (command_sendf (m, "H%d", cfg->mode) < 0)
        goto error;
    if (cfg->ihold < 0 || cfg->ihold > 100
                    || cfg->irun < 0 || cfg->irun > 100)
        goto inval;
    if (command_sendf (m, "Y%d %d", cfg->ihold, cfg->irun) < 0)
        goto error;
    if (cfg->accel < 0 || cfg->accel > 255
                    || cfg->decel < 0 || cfg->decel > 255)
        goto inval;
    if (command_sendf (m, "K%d %d", cfg->accel, cfg->decel) < 0)
        goto error;
    if (cfg->initv < 20  || cfg->initv > 20000)
        goto inval;
    if (command_sendf (m, "I%d", cfg->initv) < 0)
        goto error;
    if (cfg->finalv < 20  || cfg->finalv > 20000)
        goto inval;
    if (command_sendf (m, "V%d", cfg->finalv) < 0)
        goto error;
    if (cfg->steps < 300 || cfg->steps > 8388607)
        goto inval;
    m->cfg = *cfg;
    return 0;
inval:
    errno = EINVAL;
error:
    return -1;
}

int motion_init (struct motion *m, const char *devname,
                 struct motion_config *cfg, int flags)
{
    if ((m->fd = serial_open (devname)) < 0)
        goto error;
    ev_io_init (&m->io_w, serial_cb, m->fd, EV_READ);
    ev_timer_init (&m->status_poll_w, status_poll_cb, 0., 0.);
    ev_timer_init (&m->timeout_w, timeout_cb, 0., 0.);
    m->flags = flags;
    if (motion_reset (m) < 0)
        goto error;
    if (cfg) {
        if (motion_configure (m, cfg) < 0)
            goto error;
    }
    return 0;
error:
    if (m->fd >= 0) {
        int saved_errno = errno;
        (void)close (m->fd);
        m->fd = -1;
        errno = saved_errno;
    }
    return -1;
}

void motion_start (struct ev_loop *loop, struct motion *m)
{
    ev_io_start (loop, &m->io_w);
    m->main_loop = loop;
}

void motion_stop (struct ev_loop *loop, struct motion *m)
{
    ev_io_stop (loop, &m->io_w);
    ev_timer_stop (loop, &m->status_poll_w);
    m->main_loop = NULL;
}

const char *motion_get_name (struct motion *m)
{
    return m->name;
}

void motion_set_cb (struct motion *m, motion_cb_f cb, void *arg)
{
    m->cb = cb;
    m->cb_arg = arg;
}

void motion_destroy (struct motion *m)
{
    if (m) {
        if (m->tmp_loop)
            ev_loop_destroy (m->tmp_loop);
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
    if (!(m->tmp_loop = ev_loop_new (EVFLAG_AUTO)))
        goto error;
    return m;
error:
    motion_destroy (m);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
