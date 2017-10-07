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
#include <math.h>
#include <assert.h>

#include "log.h"
#include "xzmalloc.h"
#include "gpio.h"
#include "configfile.h"
#include "motion.h"
#include "slew.h"
#include "hpad.h"
#include "guide.h"
#include "bbox.h"
#include "lx200.h"

char *prog = "";

struct prog_context {
    struct config opt;
    struct hpad *hpad;
    struct guide *guide;
    struct bbox *bbox;
    struct lx200 *lx200;
    struct motion *t;
    struct motion *d;
    struct ev_loop *loop;
    bool t_tracking;
    int slew;
    bool west;
};

struct motion *init_axis (struct config_axis *a, const char *name, int flags,
                          bool ccw);

void hpad_cb (struct hpad *h, void *arg);
void guide_cb (struct guide *g, void *arg);
void bbox_cb (struct bbox *bb, void *arg);
void motion_cb (struct motion *m, void *arg);
void lx200_pos_cb (struct lx200 *lx, void *arg);
void lx200_slew_cb (struct lx200 *lx, void *arg);
void lx200_goto_cb (struct lx200 *lx, void *arg);
void lx200_stop_cb (struct lx200 *lx, void *arg);

int controller_velocity (struct config_axis *axis, double degrees_persec);

#define OPTIONS "+c:hMBLHGw"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {"debug-motion",         no_argument,       0, 'M'},
    {"debug-bbox",           no_argument,       0, 'B'},
    {"debug-lx200",          no_argument,       0, 'L'},
    {"debug-hpad",           no_argument,       0, 'H'},
    {"debug-guide",          no_argument,       0, 'G'},
    {"west",                 no_argument,       0, 'w'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: gem [OPTIONS]\n"
"    -c,--config FILE    set path to config file\n"
"    -w,--west           observe west of meridian (scope east of pier)\n"
"    -M,--debug-motion   emit motion control commands and responses to stderr\n"
"    -B,--debug-bbox     emit bbox protocol to stderr\n"
"    -L,--debug-lx200    emit lx200 protocol to stderr\n"
"    -H,--debug-hpad     emit hpad events to stderr\n"
"    -G,--debug-guide    emit guide pulse events to stderr\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    struct prog_context ctx;
    char *config_filename = NULL;
    int motion_flags = 0;
    int bbox_flags = 0;
    int hpad_flags = 0;
    int guide_flags = 0;
    int lx200_flags = 0;

    memset (&ctx, 0, sizeof (ctx));

    prog = basename (argv[0]);
    log_init (prog);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --config FILE */
                config_filename = xstrdup (optarg);
                break;
            case 'M':   /* --debug-motion */
                motion_flags |= MOTION_DEBUG;
                break;
            case 'B':   /* --debug-bbox */
                bbox_flags |= BBOX_DEBUG;
                break;
            case 'L':   /* --debug-lx200 */
                lx200_flags |= LX200_DEBUG;
                break;
            case 'H':   /* --debug-hpad */
                hpad_flags |= HPAD_DEBUG;
                break;
            case 'G':   /* --debug-guide */
                guide_flags |= GUIDE_DEBUG;
                break;
            case 'w':   /* --west */
                ctx.west = true;
                break;
            case 'h':   /* --help */
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();
    configfile_init (config_filename, &ctx.opt);
    if (!ctx.opt.hpad_gpio)
        msg_exit ("no hpad_gpio was configured");
    if (!ctx.opt.guide_gpio)
        msg_exit ("no guide_gpio was configured");

    if (!(ctx.loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    ctx.t = init_axis (&ctx.opt.t, "t", motion_flags, true);
    motion_set_cb (ctx.t, motion_cb, &ctx);
    motion_start (ctx.loop, ctx.t);

    ctx.d = init_axis (&ctx.opt.d, "d", motion_flags, ctx.west ? true : false);
    motion_set_cb (ctx.d, motion_cb, &ctx);
    motion_start (ctx.loop, ctx.d);

    ctx.hpad = hpad_new ();
    if (hpad_init (ctx.hpad, ctx.opt.hpad_gpio, ctx.opt.hpad_debounce,
                   hpad_cb, &ctx, hpad_flags) < 0)
        err_exit ("hpad_init");
    hpad_start (ctx.loop, ctx.hpad);

    ctx.guide = guide_new ();
    if (guide_init (ctx.guide, ctx.opt.guide_gpio, ctx.opt.guide_debounce,
                   guide_cb, &ctx, guide_flags) < 0)
        err_exit ("guide_init");
    guide_start (ctx.loop, ctx.guide);

    ctx.bbox = bbox_new ();
    if (bbox_init (ctx.bbox, DEFAULT_BBOX_PORT, bbox_cb, &ctx, bbox_flags) < 0)
        err_exit ("bbox_init");
    bbox_set_resolution (ctx.bbox, ctx.opt.t.steps, ctx.opt.d.steps);
    bbox_start (ctx.loop, ctx.bbox);

    ctx.lx200 = lx200_new ();
    if (lx200_init (ctx.lx200, DEFAULT_LX200_PORT, lx200_flags) < 0)
        err_exit ("bbox_init");
    lx200_set_position_cb (ctx.lx200, lx200_pos_cb, &ctx);
    lx200_set_slew_cb (ctx.lx200, lx200_slew_cb, &ctx);
    lx200_set_goto_cb (ctx.lx200, lx200_goto_cb, &ctx);
    lx200_set_stop_cb (ctx.lx200, lx200_stop_cb, &ctx);
    lx200_start (ctx.loop, ctx.lx200);

    ev_run (ctx.loop, 0);
    ev_loop_destroy (ctx.loop);

    bbox_stop (ctx.loop, ctx.bbox);
    bbox_destroy (ctx.bbox);

    lx200_stop (ctx.loop, ctx.lx200);
    lx200_destroy (ctx.lx200);

    guide_stop (ctx.loop, ctx.guide);
    guide_destroy (ctx.guide);

    hpad_stop (ctx.loop, ctx.hpad);
    hpad_destroy (ctx.hpad);

    motion_destroy (ctx.d);
    motion_destroy (ctx.t);

    return 0;
}

struct motion *init_axis (struct config_axis *a, const char *name, int flags,
                          bool ccw)
{
    struct motion_config cfg = {
        .resolution = a->resolution,
        .ihold      = a->ihold,
        .irun       = a->irun,
        .mode       = a->mode,
        .accel      = a->accel,
        .decel      = a->decel,
        .initv      = a->initv,
        .finalv     = a->finalv,
        .steps      = a->steps,
        .ccw        = ccw,
    };
    struct motion *m;

    if (!a->device)
        msg_exit ("%s: no serial device configured", name);
    if (!(m = motion_new (name)))
        err_exit ("%s: motion_create", name);
    if (motion_init (m, a->device, &cfg, flags) < 0)
        err_exit ("%s: motion_init %s", name, a->device);
    if (motion_set_io (m, MOTION_IO_OUTPUT1 | MOTION_IO_OUTPUT2
                                            | MOTION_IO_OUTPUT3) < 0)
        err_exit ("%s: motion set port", name);
    return m;
}

/* Given 'rate' enum from slew.h, look up configured rate in degrees/sec.
 * If 'neg' is true, make the velocity negative.
 * If 'track' is true, add the sidereal rate.
 */
double lookup_rate (struct config_axis *axis, int rate,
                    bool neg, bool track)
{
    double dps;

    switch (rate) {
        case SLEW_RATE_GUIDE:
            dps = axis->guide;
            break;
        case SLEW_RATE_SLOW:
            dps = axis->slow;
            break;
        case SLEW_RATE_MEDIUM:
            dps = axis->medium;
            break;
        case SLEW_RATE_FAST:
            dps = axis->fast;
            break;
        default:
            dps = 0.;
            break;
    }
    if (neg)
        dps *= -1.;
    if (track)
        dps += axis->sidereal;
    return dps;
}

/* A new slew "key press" event ignores a slew in progress on the
 * same axis and blindly sets the velocity.  The motion controllers can
 * handle this, even if direction is reversed.  The "key release" cancels
 * this and any other slew in progress.  The axes are independent.
 * It is possible to configure slew rates that will result in a controller
 * error;  motion_move_constant_dps() will return -1 with errno == EINVAL in
 * that case and the slew will fail.
 */
void slew_update (struct prog_context *ctx, int newmask, int rate)
{
    double dps;

    if ((newmask & SLEW_RA_PLUS) && (newmask & SLEW_RA_MINUS))
        newmask &= ~(SLEW_RA_PLUS + SLEW_RA_MINUS);
    if ((newmask & SLEW_DEC_PLUS) && (newmask & SLEW_DEC_MINUS))
        newmask &= ~(SLEW_DEC_PLUS + SLEW_DEC_MINUS);

    if ((newmask & SLEW_RA_PLUS) || (newmask & SLEW_RA_MINUS)) {
        dps = lookup_rate (&ctx->opt.t, rate, (newmask & SLEW_RA_MINUS),
                           ctx->t_tracking);
        if (motion_move_constant_dps (ctx->t, dps) < 0)
            err ("t: move at v=%.1lf*/s", dps);
    }
    else {
        if ((ctx->slew & SLEW_RA_PLUS) || (ctx->slew & SLEW_RA_MINUS)) {
            if (ctx->t_tracking) {
                dps = lookup_rate (&ctx->opt.t, 0, false, ctx->t_tracking);
                if (motion_move_constant_dps (ctx->t, dps) < 0)
                    err ("t: move at v=%.1lf*/s", dps);
            }
            else {
                if (motion_soft_stop (ctx->t) < 0) {
                    err ("t: stop");
                    if (motion_abort (ctx->t) < 0)
                        err ("t: abort");
                }
            }
        }
    }
    if ((newmask & SLEW_DEC_PLUS) || (newmask & SLEW_DEC_MINUS)) {
        dps = lookup_rate (&ctx->opt.d, rate, (newmask & SLEW_DEC_MINUS),
                           false);
        if (motion_move_constant_dps (ctx->d, dps) < 0)
            err ("d: move at v=%.1lf*/s", dps);
    }
    else {
        if ((ctx->slew & SLEW_DEC_PLUS) || (ctx->slew & SLEW_DEC_MINUS)) {
            if (motion_soft_stop (ctx->d) < 0) {
                err ("d: stop");
                if (motion_abort (ctx->d) < 0)
                    err ("d: abort");
            }
        }
    }
    ctx->slew = newmask;
}

void hpad_cb (struct hpad *h, void *arg)
{
    struct prog_context *ctx = arg;
    int dir = hpad_get_slew_direction (h);
    int rate = hpad_get_slew_rate (h);
    int ctrl = hpad_get_control (h);
    double dps;

    /* M1 - emergency stop
     */
    if ((ctrl & HPAD_CONTROL_M1)) {
        if (motion_abort (ctx->t) < 0) {
            err ("t: motion_abort");
            if (motion_abort (ctx->t) < 0) // one retry
                err ("t: motion_abort");
        }
        if (motion_abort (ctx->d) < 0) {
            err ("d: motion_abort");
            if (motion_abort (ctx->d) < 0) // one retry
                err ("t: motion_abort");
        }
        ctx->t_tracking = false;
        ctx->slew = 0;
        return;
    }
    /* M2 - toggle tracking
     */
    if ((ctrl & HPAD_CONTROL_M2)) {
        if (ctx->t_tracking) {
            if (!(ctx->slew & SLEW_RA_PLUS) && !(ctx->slew & SLEW_RA_MINUS)) {
                if (motion_soft_stop (ctx->t) < 0)
                    err ("t: stop");
            }
            ctx->t_tracking = false;
        }
        else {
            if (!(ctx->slew & SLEW_RA_PLUS) && !(ctx->slew & SLEW_RA_MINUS)) {
                dps = lookup_rate (&ctx->opt.t, SLEW_RATE_NONE, false, true);
                if (motion_move_constant_dps (ctx->t, dps) < 0)
                    err ("t: move at v=%.1lf*/s", dps);
            }
            ctx->t_tracking = true;
        }
        return;
    }
    /* N,S,E,W
     */
    slew_update (ctx, dir, rate);
}

void guide_cb (struct guide *g, void *arg)
{
    struct prog_context *ctx = arg;
    int dir;

    if ((dir = guide_get_slew_direction (g)) < 0) {
        err ("guide_get_slew_direction");
        return;
    }
    slew_update (ctx, dir, SLEW_RATE_GUIDE);
}

/* LX200 protocol notifies us that slew "button" events
 * have occurred.
 */
void lx200_slew_cb (struct lx200 *lx, void *arg)
{
    struct prog_context *ctx = arg;
    int dir = lx200_get_slew_direction (lx);
    int rate = lx200_get_slew_rate (lx);

    slew_update (ctx, dir, rate);
}

/* Bbox protocol requests that we update "encoder" position.
 */
void bbox_cb (struct bbox *bb, void *arg)
{
    struct prog_context *ctx = arg;
    double t, d;

    if (motion_get_position (ctx->t, &t) < 0) {
        err ("%s: error reading t position", __FUNCTION__);
        return;
    }
    if (motion_get_position (ctx->d, &d) < 0) {
        err ("%s: error reading d position", __FUNCTION__);
        return;
    }
    bbox_set_position (bb, t, d);
}

/* LX200 protocol requests that we update position.
 */
void lx200_pos_cb (struct lx200 *lx, void *arg)
{
    struct prog_context *ctx = arg;
    double t, d;
    double t_degrees, d_degrees;

    if (motion_get_position (ctx->t, &t) < 0) {
        err ("%s: error reading t position", __FUNCTION__);
        return;
    }
    if (motion_get_position (ctx->d, &d) < 0) {
        err ("%s: error reading d position", __FUNCTION__);
        return;
    }
    t_degrees = 360.0 * (t / ctx->opt.t.steps);
    d_degrees = 360.0 * (d / ctx->opt.d.steps);
    lx200_set_position (lx, t_degrees, d_degrees);
}

/* LX200 protocol notifies us that we should retrieve goto target
 * coordinates and slew there.
 */
void lx200_goto_cb (struct lx200 *lx, void *arg)
{
    struct prog_context *ctx = arg;
    double t_degrees, d_degrees;
    double t, d;

    lx200_get_target (lx, &t_degrees, &d_degrees);

    msg ("goto %.1f*, %.1f*", t_degrees, d_degrees);

    if (t_degrees < -90 || t_degrees > 90 || d_degrees < -90 || d_degrees > 90){
        msg ("goto out of range");
        return;
    }

    t = t_degrees/360.0 * ctx->opt.t.steps;
    d = d_degrees/360.0 * ctx->opt.d.steps;

    if (motion_goto_absolute (ctx->t, t) < 0)
        err ("t: set position");
    if (motion_goto_absolute (ctx->d, d) < 0)
        err ("d: set position");
}

/* LX200 protocol wants to stop all motion (abort a goto).
 */
void lx200_stop_cb (struct lx200 *lx, void *arg)
{
    struct prog_context *ctx = arg;

    if (motion_soft_stop (ctx->t) < 0) {
        err ("t: soft stop");
        if (motion_abort (ctx->t) < 0)
            err ("t: abort");
    }
    if (motion_soft_stop (ctx->d) < 0) {
        err ("d: soft stop");
        if (motion_abort (ctx->d) < 0)
            err ("t: abort");
    }
}

/* Motion axis informs us that goto has completed.
 * Goto cancels the constant velocity motion of RA tracking,
 * so resume it here if enabled.
 * FIXME: need to account for lost tracking for the duration of the goto.
 */
void motion_cb (struct motion *m, void *arg)
{
    struct prog_context *ctx = arg;
    double dps;

    msg ("%s: goto end", motion_get_name (m));
    if (ctx->t_tracking && m == ctx->t) {
        dps = lookup_rate (&ctx->opt.t, SLEW_RATE_NONE, false, true);
        if (motion_move_constant_dps (ctx->t, dps) < 0)
            err ("t: move at v=%.1lf*/s", dps);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
