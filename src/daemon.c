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
#include <math.h>
#include <assert.h>

#include "log.h"
#include "xzmalloc.h"
#include "gpio.h"
#include "configfile.h"
#include "motion.h"
#include "hpad.h"
#include "guide.h"
#include "bbox.h"

const double sidereal_velocity = 15.0417; /* arcsec/sec */

char *prog = "";

struct prog_context {
    struct config opt;
    struct hpad *hpad;
    struct guide *guide;
    struct bbox *bbox;
    struct motion *t;
    struct motion *d;
    struct ev_loop *loop;
    bool stopped;
    bool zeroed;
};

struct motion *init_axis (struct config_axis *a, const char *name, int flags);
int set_origin (struct motion *t, struct motion *d);
int init_origin (struct motion *t, struct motion *d);
int init_stopped (struct motion *t, struct motion *d);

void hpad_cb (struct hpad *h, void *arg);
void guide_cb (struct guide *g, void *arg);
void bbox_cb (struct bbox *bb, void *arg);

int controller_vfromarcsec (struct config_axis *axis, double arcsec_persec);
double controller_fromarcsec (struct config_axis *axis, double arcsec);
double controller_toarcsec (struct config_axis *axis, double steps);

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
    struct prog_context ctx;
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
    if (!ctx.opt.guide_gpio)
        msg_exit ("no guide_gpio was configured");

    if (!(ctx.loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    ctx.t = init_axis (&ctx.opt.t, "t", flags);
    ctx.d = init_axis (&ctx.opt.d, "d", flags);
    if ((ctx.zeroed = init_origin (ctx.t, ctx.d)) < 0)
        err_exit ("init_origin");
    if ((ctx.stopped = init_stopped (ctx.t, ctx.d)) < 0)
        err_exit ("init_stopped");

    ctx.hpad = hpad_new ();
    if (hpad_init (ctx.hpad, ctx.opt.hpad_gpio, ctx.opt.hpad_debounce,
                   hpad_cb, &ctx) < 0)
        err_exit ("hpad_init");
    hpad_start (ctx.loop, ctx.hpad);

    ctx.guide = guide_new ();
    if (guide_init (ctx.guide, ctx.opt.guide_gpio, ctx.opt.guide_debounce,
                   guide_cb, &ctx) < 0)
        err_exit ("guide_init");
    guide_start (ctx.loop, ctx.guide);

    ctx.bbox = bbox_new ();
    if (bbox_init (ctx.bbox, DEFAULT_BBOX_PORT, bbox_cb, &ctx, 0) < 0)
        err_exit ("bbox_init");
    bbox_set_resolution (ctx.bbox, ctx.opt.t.steps, ctx.opt.d.steps);
    bbox_start (ctx.loop, ctx.bbox);

    ev_run (ctx.loop, 0);
    ev_loop_destroy (ctx.loop);

    bbox_stop (ctx.loop, ctx.bbox);
    bbox_destroy (ctx.bbox);

    guide_stop (ctx.loop, ctx.guide);
    guide_destroy (ctx.guide);

    hpad_stop (ctx.loop, ctx.hpad);
    hpad_destroy (ctx.hpad);

    if (ctx.d)
        motion_fini (ctx.d);
    if (ctx.t)
        motion_fini (ctx.t);

    return 0;
}

int init_stopped (struct motion *t, struct motion *d)
{
    uint8_t a, b;
    if (motion_get_status (t, &a) < 0)
        return -1;
    if (motion_get_status (d, &b) < 0)
        return -1;
    return (a == 0 && b == 0);
}

/* The green LED goes off after the axes are zeroed
 * by calling set_origin() - M1 + M2 buttons on the handpad.
 * The LED state which lives in the motion controller
 * persists across a daemon restart, so get the initial
 * state of zeroed/not zeroed by reading the LED state.
 */

int init_origin (struct motion *t, struct motion *d)
{
    uint8_t a, b;
    if (motion_get_port (t, &a) < 0)
        return -1;
    if (motion_get_port (d, &b) < 0)
        return -1;
    return ((a & GREEN_LED_MASK) != 0 && (b & GREEN_LED_MASK) != 0);
}

int set_origin (struct motion *t, struct motion *d)
{
    if (motion_set_origin (t) < 0)
        return -1;
    if (motion_set_origin (d) < 0)
        return -1;
    if (motion_set_port (t, GREEN_LED_MASK) < 0)
        return -1;
    if (motion_set_port (d, GREEN_LED_MASK) < 0)
        return -1;
    return 0;
}

struct motion *init_axis (struct config_axis *a, const char *name, int flags)
{
    struct motion *m;
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

void hpad_cb (struct hpad *h, void *arg)
{
    struct prog_context *ctx = arg;
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
            break;
        }
        case HPAD_KEY_NORTH: {
            int v = controller_vfromarcsec (&ctx->opt.d,
                                fast ? ctx->opt.d.fast : ctx->opt.d.slow);
            if (motion_set_velocity (ctx->d, v) < 0)
                err_exit ("d: set velocity");
            break;
        }
        case HPAD_KEY_SOUTH: {
            int v = controller_vfromarcsec (&ctx->opt.d,
                                fast ? ctx->opt.d.fast : ctx->opt.d.slow);
            if (motion_set_velocity (ctx->d, -1*v) < 0)
                err_exit ("d: set velocity");
            break;
        }
        case HPAD_KEY_WEST: {
            int v = controller_vfromarcsec (&ctx->opt.t,
                                fast ? ctx->opt.t.fast : ctx->opt.t.slow);
            if (motion_set_velocity (ctx->t, v) < 0)
                err_exit ("t: set velocity");
            break;
        }
        case HPAD_KEY_EAST: {
            int v = controller_vfromarcsec (&ctx->opt.t,
                                fast ? ctx->opt.t.fast : ctx->opt.t.slow);
            if (motion_set_velocity (ctx->t, -1*v) < 0)
                err_exit ("t: set velocity");
            break;
        }
        case (HPAD_KEY_M1 | HPAD_KEY_M2): /* zero */
            if (set_origin (ctx->t, ctx->d) < 0)
                err_exit ("set origin");
            ctx->stopped = true;
            ctx->zeroed = true;
            break;
        case HPAD_KEY_M1: /* unused */
            break;
        case HPAD_KEY_M2: /* toggle stop */
            ctx->stopped = !ctx->stopped;
            break;
    }
}

void guide_cb (struct guide *g, void *arg)
{
    struct prog_context *ctx = arg;
    int val;

    if ((val = guide_read (g)) < 0)
        err_exit ("guide");
    if (ctx->opt.debug)
        msg ("guide: %d", val);
}

/* Bbox protocol requests that we update "encoder" position.
 */
void bbox_cb (struct bbox *bb, void *arg)
{
    struct prog_context *ctx = arg;
    double x, y;

    if (motion_get_position (ctx->t, &x) < 0) {
        err ("%s: error reading t position", __FUNCTION__);
        return;
    }
    if (motion_get_position (ctx->d, &y) < 0) {
        err ("%s: error reading d position", __FUNCTION__);
        return;
    }
    bbox_set_position (bb, (int)x, (int)y);
}

/* Return position in arcsec from controller steps
 */
double controller_toarcsec (struct config_axis *axis, double steps)
{
    return steps * (360.0*60*60) / axis->steps + axis->offset;
}

/* Calculate position in steps for motion controller from arcsec.
 */
double controller_fromarcsec (struct config_axis *axis, double arcsec)
{
    return (arcsec - axis->offset) * axis->steps / (360.0*60*60);
}

/* Calculate velocity in steps/sec for motion controller from arcsec/sec.
 * Take into account controller velocity scaling in 'auto' mode.
 */
int controller_vfromarcsec (struct config_axis *axis, double arcsec_persec)
{
    double steps_persec = arcsec_persec * axis->steps / (360.0*60*60);

    if (axis->mode == 1)
        steps_persec *= 1<<(axis->resolution);
    return lrint (steps_persec);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
