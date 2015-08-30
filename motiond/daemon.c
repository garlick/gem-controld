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

/* Note: in this module, (t,d) refers to raw telescope postion in
 * arcseconds.  Origin is (0,324000), roughly (LHA,DEC) = (0,+90),
 * telescope on west side of pier.
 *
 * (x,y) refers to telescope position in whole steps.  Origin (0,0).

 * f refers to focus position in microns (origin 0, full ccw/out)
 * z refers to focus position in whole steps
 */

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

const double sidereal_velocity = 15.0417; /* arcsec/sec */

const double pub_slow = 5; /* sec */
const double pub_fast = 0.5; /* sec */

char *prog = "";

typedef struct {
    opt_t opt;
    hpad_t hpad;
    motion_t t, d, f;
    zsock_t *zreq;
    zsock_t *zpub;
    struct ev_loop *loop;
    ev_zmq req_watcher;
    ev_timer pub_w;
    bool stopped;
    bool zeroed;
    bool focusmode;
} ctx_t;

motion_t init_axis (opt_axis_t *a, const char *name, int flags);
int set_origin (ctx_t *ctx);
int init_origin (ctx_t *ctx);
int init_stopped (ctx_t *ctx);

void hpad_cb (hpad_t h, void *arg);
void zreq_cb (struct ev_loop *loop, ev_zmq *w, int revents);
void pub_cb (struct ev_loop *loop, ev_timer *w, int revents);

int controller_vfromarcsec (opt_axis_t *axis, double arcsec_persec);
double controller_fromarcsec (opt_axis_t *axis, double arcsec);
double controller_toarcsec (opt_axis_t *axis, double steps);

int controller_vfrommicrons (opt_axis_t *axis, double microns_persec);
double controller_frommicrons (opt_axis_t *axis, double microns);
double controller_tomicrons (opt_axis_t *axis, double steps);


bool safeposition (ctx_t *ctx, double t, double d);

bool motion_inprogress (ctx_t *ctx);

int start_goto (ctx_t *ctx, double t, double d);
int start_focus (ctx_t *ctx, double f);

#define OPTIONS "+c:hdf"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {"debug",                no_argument,       0, 'd'},
    {"force",                no_argument,       0, 'f'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: gem [OPTIONS]\n"
"    -c,--config FILE    set path to config file\n"
"    -d,--debug          emit verbose debugging to stderr\n"
"    -f,--force          force motion controller reset (must reset origin)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    ctx_t ctx;
    char *config_filename = NULL;
    int flags = 0;

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
                flags |= MOTION_DEBUG;
                break;
            case 'f':   /* --force */
                flags |= MOTION_RESET;
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
    if (!ctx.opt.req_bind_uri)
        msg_exit ("no req_bind_uri was configured");
    if (!ctx.opt.pub_bind_uri)
        msg_exit ("no pub_bind_uri was configured");

    if (!(ctx.loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    ctx.t = init_axis (&ctx.opt.t, "t", flags);
    ctx.d = init_axis (&ctx.opt.d, "d", flags);
    ctx.f = init_axis (&ctx.opt.f, "f", flags);
    if (init_origin (&ctx) < 0)
        err_exit ("init_origin");
    if (init_stopped (&ctx) < 0)
        err_exit ("init_stopped");

    ctx.hpad = hpad_new ();
    if (hpad_init (ctx.hpad, ctx.opt.hpad_gpio, ctx.opt.hpad_debounce,
                   hpad_cb, &ctx) < 0)
        err_exit ("hpad_init");
    hpad_start (ctx.loop, ctx.hpad);

    setenv ("ZSYS_LINGER", "10", 1);
    if (!(ctx.zreq = zsock_new_router (ctx.opt.req_bind_uri)))
        err_exit ("zsock_new_router %s", ctx.opt.req_bind_uri);
    if (ev_zmq_init (&ctx.req_watcher, zreq_cb,
                     zsock_resolve (ctx.zreq), EV_READ) < 0)
        err_exit ("ev_zmq_init");
    ev_zmq_start (ctx.loop, &ctx.req_watcher);

    if (!(ctx.zpub = zsock_new_pub (ctx.opt.pub_bind_uri)))
        err_exit ("zsock_new_pub %s", ctx.opt.pub_bind_uri);

    ev_timer_init (&ctx.pub_w, pub_cb, pub_slow, pub_slow);
    ev_timer_start (ctx.loop, &ctx.pub_w);

    zsys_handler_set (NULL); /* disable zeromq signal handling */

    ev_run (ctx.loop, 0);
    ev_loop_destroy (ctx.loop);

    hpad_stop (ctx.loop, ctx.hpad);
    hpad_destroy (ctx.hpad);

    if (ctx.d)
        motion_fini (ctx.d);
    if (ctx.t)
        motion_fini (ctx.t);
    if (ctx.f)
        motion_fini (ctx.f);

    zsock_destroy (&ctx.zreq);

    return 0;
}

int update_position_msg (ctx_t *ctx, gmsg_t g, bool *moving)
{
    double x, y, z;
    uint8_t a, b, c;
    uint32_t flags = 0;
    int rc = -1;

    assert (ctx->zeroed == true);

    if (motion_get_position (ctx->t, &x) < 0)
        goto done;
    if (motion_get_position (ctx->d, &y) < 0)
        goto done;
    if (motion_get_position (ctx->f, &z) < 0)
        goto done;
    double t = controller_toarcsec (&ctx->opt.t, x);
    double d = controller_toarcsec (&ctx->opt.d, y);
    double f = controller_toarcsec (&ctx->opt.f, z);
    if (motion_get_status (ctx->t, &a) < 0)
        goto done;
    if (motion_get_status (ctx->d, &b) < 0)
        goto done;
    if (motion_get_status (ctx->f, &c) < 0)
        goto done;
    if ((a & MOTION_STATUS_TRACKING))
        flags |= FLAG_T_TRACKING;
    if ((a & MOTION_STATUS_MOVING))
        flags |= FLAG_T_MOVING;
    if ((b & MOTION_STATUS_TRACKING))
        flags |= FLAG_D_TRACKING;
    if ((b & MOTION_STATUS_MOVING))
        flags |= FLAG_D_MOVING;
    if ((c & MOTION_STATUS_TRACKING))
        flags |= FLAG_F_TRACKING;
    if ((c & MOTION_STATUS_MOVING))
        flags |= FLAG_F_MOVING;
    if (gmsg_set_flags (g, flags) < 0)
        goto done;
    if (gmsg_set_arg1 (g, (int32_t)(1E2*t)) < 0)
        goto done;
    if (gmsg_set_arg2 (g, (int32_t)(1E2*d)) < 0)
        goto done;
    if (gmsg_set_arg3 (g, (int32_t)f) < 0)
        goto done;
    if (moving)
	*moving = ((a & MOTION_STATUS_MOVING) || (b & MOTION_STATUS_MOVING));
    rc = 0;
done:
    return rc;
}

void pub_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    ctx_t *ctx = (ctx_t *)((char *)w - offsetof (ctx_t, pub_w));
    gmsg_t g = NULL;
    bool moving;

    if (!ctx->zeroed)
        goto done;
    if (!(g = gmsg_create (OP_POSITION)))
        goto done;
    if (update_position_msg (ctx, g, &moving) < 0)
        goto done;
    if (gmsg_send (ctx->zpub, g) < 0)
        goto done;
    w->repeat = moving ? pub_fast : pub_slow;
done:
    gmsg_destroy (&g);
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
            if (update_position_msg (ctx, g, NULL) < 0)
                goto done;
            rc = 0;
            break;
        }
        case OP_STOP: {
            if (motion_stop (ctx->t) < 0 || motion_stop (ctx->d) < 0
                                         || motion_stop (ctx->f) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_TRACK: {
            uint32_t flags;
            int vx = controller_vfromarcsec (&ctx->opt.t, sidereal_velocity);
            int vy = 0;
            if (gmsg_get_flags (g, &flags) < 0)
                goto done;
            if ((flags & FLAG_ARG1)) {
                int32_t arg;
                if (gmsg_get_arg1 (g, &arg) < 0)
                    goto done;
                vx = controller_vfromarcsec (&ctx->opt.t, 1E-2*arg);
            }
            if ((flags & FLAG_ARG2)) {
                int32_t arg;
                if (gmsg_get_arg2 (g, &arg) < 0)
                    goto done;
                vy = controller_vfromarcsec (&ctx->opt.d, 1E-2*arg);
            }
            if (motion_set_velocity (ctx->t, vx) < 0)
                goto done;
            if (motion_set_velocity (ctx->d, vy) < 0)
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
            double t, d;
            if (gmsg_get_arg1 (g, &arg1) < 0)
                goto done;
            if (gmsg_get_arg2 (g, &arg2) < 0)
                goto done;
            t = 1E-2*arg1;
            d = 1E-2*arg2;
            if (!ctx->zeroed || !safeposition (ctx, t, d)) {
                errno = EINVAL;
                goto done;
            }
            if (motion_inprogress (ctx)) {
                errno = EAGAIN;
                goto done;
            }
            if (start_goto (ctx, t, d) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_PARK: {
            double t = ctx->opt.t.park;
            double d = ctx->opt.d.park;
            if (!ctx->zeroed || !safeposition (ctx, t, d)) {
                errno = EINVAL;
                goto done;
            }
            if (motion_inprogress (ctx)) {
                errno = EAGAIN;
                goto done;
            }
            if (start_goto (ctx, t, d) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_FOCUS: {
            int32_t arg3;
            double f;
            if (gmsg_get_arg3 (g, &arg3) < 0)
                goto done;
            f = (double)arg3;
            if (motion_inprogress (ctx) < 0) {
                errno = EAGAIN;
                goto done;
            }
            if (start_focus (ctx, f) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
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

int init_stopped (ctx_t *ctx)
{
    uint8_t a, b;
    if (motion_get_status (ctx->t, &a) < 0)
        return -1;
    if (motion_get_status (ctx->d, &b) < 0)
        return -1;
    ctx->stopped = (a == 0 && b == 0);
    return 0;
}

int init_origin (ctx_t *ctx)
{
    uint8_t a, b, f;
    if (motion_get_port (ctx->t, &a) < 0)
        return -1;
    if (motion_get_port (ctx->d, &b) < 0)
        return -1;
    if (motion_get_port (ctx->f, &f) < 0)
        return -1;
    ctx->zeroed = ((a & GREEN_LED_MASK) != 0 && (b & GREEN_LED_MASK) != 0
                && (f & GREEN_LED_MASK) != 0);
    return 0;
}

int set_origin (ctx_t *ctx)
{
    if (motion_set_origin (ctx->t) < 0)
        return -1;
    if (motion_set_origin (ctx->d) < 0)
        return -1;
    if (motion_set_origin (ctx->f) < 0)
        return -1;
    if (motion_set_port (ctx->t, GREEN_LED_MASK) < 0)
        return -1;
    if (motion_set_port (ctx->d, GREEN_LED_MASK) < 0)
        return -1;
    if (motion_set_port (ctx->f, GREEN_LED_MASK) < 0)
        return -1;
    ctx->stopped = true;
    ctx->zeroed = true;
    return 0;
}

motion_t init_axis (opt_axis_t *a, const char *name, int flags)
{
    motion_t m;
    bool coldstart;

    if (!a->device)
        msg_exit ("%s: no serial device configured", name);
    if (!(m = motion_init (a->device, name, flags, &coldstart)))
        err_exit ("%s: init %s", name, a->device);
    if (coldstart) {
        if (motion_set_current (m, a->ihold, a->irun) < 0)
            err_exit ("%s: set current", name);
        if (motion_set_mode (m, a->mode) < 0)
            err_exit ("%s: set mode", name);
        if (motion_set_resolution (m, a->resolution) < 0)
            err_exit ("%s: set resolution", name);
        if (motion_set_acceleration (m, a->accel, a->decel) < 0)
            err_exit ("%s: set acceleration", name);
        if (motion_set_initial_velocity (m, a->initv) < 0)
            err_exit ("%s: set initial velocity", name);
        if (motion_set_final_velocity (m, a->finalv) < 0)
            err_exit ("%s: set final velocity", name);
    }
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
            int vx = 0;
            if (!ctx->stopped && ctx->zeroed)
                vx = controller_vfromarcsec (&ctx->opt.t, sidereal_velocity);
            if (motion_set_velocity (ctx->t, vx) < 0)
                err_exit ("t: set velocity");
            if (motion_set_velocity (ctx->d, 0) < 0)
                err_exit ("d: set velocity");
            if (motion_set_velocity (ctx->f, 0) < 0)
                err_exit ("f: set velocity");
            break;
        }
        case HPAD_KEY_NORTH: {
            if (ctx->focusmode) {
                int v = controller_vfrommicrons (&ctx->opt.f,
                                    fast ? ctx->opt.f.fast : ctx->opt.f.slow);
                if (motion_set_velocity (ctx->f, v) < 0)
                    err_exit ("f: set velocity");
            } else {
                int v = controller_vfromarcsec (&ctx->opt.d,
                                    fast ? ctx->opt.d.fast : ctx->opt.d.slow);
                if (motion_set_velocity (ctx->d, v) < 0)
                    err_exit ("d: set velocity");
            }
            break;
        }
        case HPAD_KEY_SOUTH: {
            if (ctx->focusmode) {
                int v = controller_vfrommicrons (&ctx->opt.f,
                                    fast ? ctx->opt.f.fast : ctx->opt.f.slow);
                if (motion_set_velocity (ctx->f, -1*v) < 0)
                    err_exit ("f: set velocity");
            } else {
                int v = controller_vfromarcsec (&ctx->opt.d,
                                    fast ? ctx->opt.d.fast : ctx->opt.d.slow);
                if (motion_set_velocity (ctx->d, -1*v) < 0)
                    err_exit ("d: set velocity");
            }
            break;
        }
        case HPAD_KEY_WEST: {
            if (!ctx->focusmode) {
                int v = controller_vfromarcsec (&ctx->opt.t,
                                    fast ? ctx->opt.t.fast : ctx->opt.t.slow);
                if (motion_set_velocity (ctx->t, v) < 0)
                    err_exit ("t: set velocity");
            }
            break;
        }
        case HPAD_KEY_EAST: {
            if (!ctx->focusmode) {
                int v = controller_vfromarcsec (&ctx->opt.t,
                                    fast ? ctx->opt.t.fast : ctx->opt.t.slow);
                if (motion_set_velocity (ctx->t, -1*v) < 0)
                    err_exit ("t: set velocity");
            }
            break;
        }
        case (HPAD_KEY_M1 | HPAD_KEY_M2): /* zero */
            if (set_origin (ctx) < 0)
                err_exit ("set origin");
            break;
        case HPAD_KEY_M1: /* toggle focus/mount mode */
            ctx->focusmode = !ctx->focusmode;
            break;
        case HPAD_KEY_M2: /* toggle stop */
            ctx->stopped = !ctx->stopped;
            break;
    }
}

/* Return position in microns from controller steps.
 * (microns are units for focus axis position)
 */
double controller_tomicrons (opt_axis_t *axis, double steps)
{
    return steps * 1E6 / axis->steps;
}

/* Calculate position in steps for motion controller from microns.
 * (microns are units for focus axis position)
 */
double controller_frommicrons (opt_axis_t *axis, double microns)
{
    return microns * axis->steps * 1E-6;
}

int controller_vfrommicrons (opt_axis_t *axis, double microns_persec)
{
    double steps_persec = microns_persec * axis->steps * 1E-6;

    if (axis->mode == 1)
        steps_persec *= 1<<(axis->resolution);
    return lrint (steps_persec);
}

/* Return position in arcsec from controller steps
 */
double controller_toarcsec (opt_axis_t *axis, double steps)
{
    return steps * (360.0*60*60) / axis->steps + axis->offset;
}

/* Calculate position in steps for motion controller from arcsec.
 */
double controller_fromarcsec (opt_axis_t *axis, double arcsec)
{
    return (arcsec - axis->offset) * axis->steps / (360.0*60*60);
}

/* Calculate velocity in steps/sec for motion controller from arcsec/sec.
 * Take into account controller velocity scaling in 'auto' mode.
 */
int controller_vfromarcsec (opt_axis_t *axis, double arcsec_persec)
{
    double steps_persec = arcsec_persec * axis->steps / (360.0*60*60);

    if (axis->mode == 1)
        steps_persec *= 1<<(axis->resolution);
    return lrint (steps_persec);
}

int start_goto (ctx_t *ctx, double t, double d)
{
    assert (ctx->zeroed == true);

    double x = controller_fromarcsec (&ctx->opt.t, t);
    double y = controller_fromarcsec (&ctx->opt.d, d);
    int rc = -1;

    if (motion_set_position (ctx->t, x) < 0)
        goto done;
    if (motion_set_position (ctx->d, y) < 0)
        goto done;
    ctx->pub_w.repeat = pub_fast;
    ev_timer_again (ctx->loop, &ctx->pub_w);
    ctx->stopped = true; /* client should restart tracking after goto */
    rc = 0;
done:
    return rc;
}

int start_focus (ctx_t *ctx, double f)
{
    double z = controller_frommicrons (&ctx->opt.f, f);
    int rc = -1;

    if (motion_set_index (ctx->f, z) < 0)
        goto done;
    ctx->pub_w.repeat = pub_fast;
    ev_timer_again (ctx->loop, &ctx->pub_w);
    rc = 0;
done:
    return rc;
}

bool motion_inprogress (ctx_t *ctx)
{
    uint8_t s;

    if ((motion_get_status (ctx->t, &s) == 0 && (s & MOTION_STATUS_MOVING))
     || (motion_get_status (ctx->d, &s) == 0 && (s & MOTION_STATUS_MOVING))
     || (motion_get_status (ctx->f, &s) == 0 && (s & MOTION_STATUS_MOVING)))
        return true;
    return false;
}

bool safeposition (ctx_t *ctx, double t, double d)
{
    if (t < ctx->opt.t.low_limit || t > ctx->opt.t.high_limit
     || d < ctx->opt.d.low_limit || d > ctx->opt.d.high_limit)
        return false;
    return true;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
