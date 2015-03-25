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
#include <unistd.h>
#include <libgen.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <ev.h>
#include <zmq.h>
#include <czmq.h>
#include <math.h>

#include "libutil/log.h"
#include "libutil/xzmalloc.h"
#include "libutil/gpio.h"
#include "libutil/ev_zmq.h"
#include "libcommon/configfile.h"
#include "libcommon/gmsg.h"

#include "motion.h"
#include "hpad.h"

const double sidereal_velocity = -15.0417; /* arcsec/sec */

char *prog = "";

typedef struct {
    opt_t opt;
    hpad_t hpad;
    motion_t ra, dec;
    zsock_t *zreq;
    ev_zmq req_watcher;
    bool stopped;
    bool zeroed;
} ctx_t;

void hpad_cb (hpad_t h, void *arg);

motion_t init_axis (opt_axis_t *a, const char *name, int flags);

void zreq_cb (struct ev_loop *loop, ev_zmq *w, int revents);

int controller_velocity (opt_axis_t *axis, double arcsec_persec);
double controller_position (opt_axis_t *axis, double arcsec);
double arcsec_fromcontroller (opt_axis_t *axis, double steps);

int set_origin (ctx_t *ctx);
int init_origin (ctx_t *ctx);

#define OPTIONS "+c:hd"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {"debug",                no_argument,       0, 'd'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: gem [OPTIONS]\n"
"    -c,--config FILE         set path to config file\n"
"    -d,--debug               emit verbose debugging to stderr\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    struct ev_loop *loop;
    int ch;
    ctx_t ctx;
    char *config_filename = NULL;

    memset (&ctx, 0, sizeof (ctx));

    prog = basename (argv[0]);
    log_init (prog);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --config FILE */
                config_filename = xstrdup (optarg);
                break;
        }
    }
    configfile_init (config_filename, &ctx.opt);

    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --config FILE (handled above) */
                break;
            case 'd':   /* --debug */
                ctx.opt.debug = true;
                break;
            case 'h':   /* --help */
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();
    if (!ctx.opt.hpad_gpio)
        msg_exit ("no hpad_gpio was configured");
    if (!ctx.opt.req_uri)
        msg_exit ("no req uri was configured");

    if (!(loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    int flags = 0;
    if (ctx.opt.debug)
        flags |= MOTION_DEBUG;
    ctx.ra = init_axis (&ctx.opt.ra, "RA", flags);
    ctx.dec = init_axis (&ctx.opt.dec, "DEC", flags);
    if (init_origin (&ctx) < 0)
        err_exit ("init_origin");

    ctx.hpad = hpad_new ();
    if (hpad_init (ctx.hpad, ctx.opt.hpad_gpio, ctx.opt.hpad_debounce,
                   hpad_cb, &ctx) < 0)
        err_exit ("hpad_init");
    hpad_start (loop, ctx.hpad);

    setenv ("ZSYS_LINGER", "10", 1);
    if (!(ctx.zreq = zsock_new_router (ctx.opt.req_uri)))
        err_exit ("zsock_new_router %s", ctx.opt.req_uri);
    if (ev_zmq_init (&ctx.req_watcher, zreq_cb,
                     zsock_resolve (ctx.zreq), EV_READ) < 0)
        err_exit ("ev_zmq_init");
    ev_zmq_start (loop, &ctx.req_watcher);
    zsys_handler_set (NULL); /* disable zeromq signal handling */

    ev_run (loop, 0); 
    ev_loop_destroy (loop);

    hpad_stop (loop, ctx.hpad);
    hpad_destroy (ctx.hpad);

    if (ctx.dec)
        motion_fini (ctx.dec); 
    if (ctx.ra)
        motion_fini (ctx.ra); 

    zsock_destroy (&ctx.zreq);

    return 0;
}

void zreq_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    ctx_t *ctx = (ctx_t *)((char *)w - offsetof (ctx_t, req_watcher));
    int rc = -1;
    gmsg_t g;
    uint8_t op;

    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done_noreply;
    }

    if (ctx->opt.debug)
        gmsg_dump (stderr, g, "recv");
    if (gmsg_get_op (g, &op) < 0)
        goto done;
    switch (op) {
        case OP_POSITION: {
            if (!ctx->zeroed) {
                errno = EINVAL;
                goto done;
            }
            double ra, dec;
            if (motion_get_position (ctx->ra, &ra) < 0)
                goto done;
            if (motion_get_position (ctx->dec, &dec) < 0)
                goto done;
            double x = arcsec_fromcontroller (&ctx->opt.ra, ra);
            double y = arcsec_fromcontroller (&ctx->opt.dec, dec);
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            if (gmsg_set_arg1 (g, (int32_t)(x*10)) < 0)
                goto done;
            if (gmsg_set_arg2 (g, (int32_t)(y*10)) < 0)
                goto done;
            rc = 0;
            break;
        }
        case OP_STOP: {
            if (motion_stop (ctx->ra) < 0 || motion_stop (ctx->dec) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_TRACK: {
            uint32_t flags;
            int y = 0;
            int x = controller_velocity (&ctx->opt.ra, sidereal_velocity);
            if (gmsg_get_flags (g, &flags) < 0)
                goto done;
            if ((flags & FLAG_ARG1)) {
                int32_t arg;
                if (gmsg_get_arg1 (g, &arg) < 0)
                    goto done;
                x = controller_velocity (&ctx->opt.ra, 0.1*arg);
            }
            if ((flags & FLAG_ARG2)) {
                int32_t arg;
                if (gmsg_get_arg2 (g, &arg) < 0)
                    goto done;
                y = controller_velocity (&ctx->opt.dec, 0.1*arg);
            }
            if (motion_set_velocity (ctx->ra, x) < 0)
                goto done;
            if (motion_set_velocity (ctx->dec, y) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = false;
            rc = 0;
            break;
        }
        case OP_ORIGIN: {
            if (set_origin (ctx) < 0)
                goto done;
            rc = 0;
            break;
        }
        case OP_GOTO: {
            int32_t arg1, arg2;
            double x, y;
            if (!ctx->zeroed) {
                errno = EINVAL;
                goto done;
            }
            if (gmsg_get_arg1 (g, &arg1) < 0)
                goto done;
            if (gmsg_get_arg2 (g, &arg2) < 0)
                goto done;
            x = controller_position (&ctx->opt.ra, 0.1*arg1);
            y = controller_position (&ctx->opt.dec, 0.1*arg2);
            if (motion_set_position (ctx->ra, x) < 0)
                goto done;
            if (motion_set_position (ctx->dec, y) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_PARK: { /* FIXME: make park position configurable */
            if (!ctx->zeroed) {
                errno = EINVAL;
                goto done;
            }
            if (motion_set_position (ctx->ra, 0) < 0)
                goto done;
            if (motion_set_position (ctx->dec, 0) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
    }
done:
    if (rc != 0 && gmsg_set_error (g, errno) < 0) {
        err ("gmsg_set_error");
        goto done_noreply;
    }
    if (ctx->opt.debug)
        gmsg_dump (stderr, g, "send");
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done_noreply;
    }
done_noreply:
    gmsg_destroy (&g);
    return;
}

int init_origin (ctx_t *ctx)
{
    uint8_t x, y;
    if (motion_get_port (ctx->ra, &x) < 0)
        return -1;
    if (motion_get_port (ctx->dec, &y) < 0)
        return -1;
    ctx->zeroed = ((x & GREEN_LED_MASK) != 0 && (y & GREEN_LED_MASK) != 0);
    return 0;
}

int set_origin (ctx_t *ctx)
{
    if (motion_set_origin (ctx->ra) < 0)
        return -1;
    if (motion_set_origin (ctx->dec) < 0)
        return -1;
    if (motion_set_port (ctx->ra, GREEN_LED_MASK) < 0)
        return -1;
    if (motion_set_port (ctx->dec, GREEN_LED_MASK) < 0)
        return -1;
    ctx->stopped = true;
    ctx->zeroed = true;
    return 0;
}

motion_t init_axis (opt_axis_t *a, const char *name, int flags)
{
    motion_t m;
    if (!a->device)
        msg_exit ("%s: no serial device configured", name);
    if (!(m = motion_init (a->device, name, flags)))
        err_exit ("%s: init %s", name, a->device);
    if (motion_set_current (m, a->ihold, a->irun) < 0)
        err_exit ("%s: set current", name);
    if (motion_set_mode (m, a->mode) < 0)
        err_exit ("%s: set mode", name);
    if (motion_set_resolution (m, a->resolution) < 0)
        err_exit ("%s: set resolution", name);
    if (motion_set_acceleration (m, a->accel, a->decel) < 0)
        err_exit ("%s: set acceleration", name);
    return m;
}

void hpad_cb (hpad_t h, void *arg)
{
    ctx_t *ctx = arg;
    int val;

    if ((val = hpad_read (h)) < 0)
        err_exit ("hpad");
    if (ctx->opt.debug)
        msg ("hpad: %d", val);

    bool fast = (val & HPAD_MASK_FAST);
    switch (val & HPAD_MASK_KEYS) {
        case HPAD_KEY_NONE: {
            int x = 0;
            if (!ctx->stopped && ctx->zeroed)
                x = controller_velocity (&ctx->opt.ra, sidereal_velocity);
            if (motion_set_velocity (ctx->ra, x) < 0)
                err_exit ("ra: set velocity");
            if (motion_set_velocity (ctx->dec, 0) < 0)
                err_exit ("dec: set velocity");
            break;
        }
        case HPAD_KEY_NORTH: {
            int v = controller_velocity (&ctx->opt.dec,
                                                    fast ? ctx->opt.dec.fast
                                                         : ctx->opt.dec.slow);
            if (motion_set_velocity (ctx->dec, v) < 0)
                err_exit ("dec: set velocity");
            break;
        }
        case HPAD_KEY_SOUTH: {
            int v = controller_velocity (&ctx->opt.dec,
                                                    fast ? ctx->opt.dec.fast
                                                         : ctx->opt.dec.slow);
            if (motion_set_velocity (ctx->dec, -1*v) < 0)
                err_exit ("dec: set velocity");
            break;
        }
        case HPAD_KEY_WEST: {
            int v = controller_velocity (&ctx->opt.ra,
                                                    fast ? ctx->opt.ra.fast
                                                         : ctx->opt.ra.slow);
            if (motion_set_velocity (ctx->ra, v) < 0)
                err_exit ("ra: set velocity");
            break;
        }
        case HPAD_KEY_EAST: {
            int v = controller_velocity (&ctx->opt.ra,
                                                    fast ? ctx->opt.ra.fast
                                                         : ctx->opt.ra.slow);
            if (motion_set_velocity (ctx->ra, -1*v) < 0)
                err_exit ("ra: set velocity");
            break;
        } 
        case HPAD_KEY_M1: /* zero */
            if (set_origin (ctx) < 0)
                err_exit ("set origin");
            break;
        case HPAD_KEY_M2: /* toggle stop */
            ctx->stopped = !ctx->stopped;
            break;
    }
}

/* Return position in arcsec from controller steps
 */
double arcsec_fromcontroller (opt_axis_t *axis, double steps)
{
    return steps * (360.0*60*60) / axis->steps;
}

/* Calculate position in steps for motion controller from arcsec.
 */
double controller_position (opt_axis_t *axis, double arcsec)
{
    return arcsec * axis->steps / (360.0*60*60);
}

/* Calculate velocity in steps/sec for motion controller from arcsec/sec.
 * Take into account controller velocity scaling in 'auto' mode.
 */
int controller_velocity (opt_axis_t *axis, double arcsec_persec)
{
    double steps_persec = controller_position (axis, arcsec_persec);

    if (axis->mode == 1)
        steps_persec *= 1<<(axis->resolution);
    return lrint (steps_persec);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
