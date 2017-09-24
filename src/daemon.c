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
    bool west;
};

struct motion *init_axis (struct config_axis *a, const char *name, int flags);

void hpad_cb (struct hpad *h, void *arg);
void guide_cb (struct guide *g, void *arg);
void bbox_cb (struct bbox *bb, void *arg);
void lx200_pos_cb (struct lx200 *lx, void *arg);
void lx200_slew_cb (struct lx200 *lx, void *arg);
void lx200_goto_cb (struct lx200 *lx, void *arg);

int controller_velocity (struct config_axis *axis, double degrees_persec);

#define OPTIONS "+c:hMBLHGwf"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {"debug-motion",         no_argument,       0, 'M'},
    {"debug-bbox",           no_argument,       0, 'B'},
    {"debug-lx200",          no_argument,       0, 'L'},
    {"debug-hpad",           no_argument,       0, 'H'},
    {"debug-guide",          no_argument,       0, 'G'},
    {"west",                 no_argument,       0, 'w'},
    {"force",                no_argument,       0, 'f'},
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
"    -f,--force          force motion controller reset (must reset origin)\n"
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
        }
    }
    configfile_init (config_filename, &ctx.opt);

    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --config FILE (handled above) */
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
            case 'f':   /* --force */
                motion_flags |= MOTION_RESET;
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
    if (!ctx.opt.hpad_gpio)
        msg_exit ("no hpad_gpio was configured");
    if (!ctx.opt.guide_gpio)
        msg_exit ("no guide_gpio was configured");

    if (!(ctx.loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    ctx.t = init_axis (&ctx.opt.t, "t", motion_flags);
    ctx.d = init_axis (&ctx.opt.d, "d", motion_flags);

    /* Initialize t_tracking state from actual state of t motion controller.
     * If the daemon was restarted, we should retain this state.
     */
    uint8_t t_status;
    if (motion_get_status (ctx.t, &t_status) < 0)
        err_exit ("motion_get_status t");
    if (t_status != 0)
        ctx.t_tracking = true;
    msg ("tracking in RA is %s - press handpad M2 button to toggle",
          ctx.t_tracking ? "enabled" : "disabled");

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

    if (ctx.d)
        motion_fini (ctx.d);
    if (ctx.t)
        motion_fini (ctx.t);

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
        if (motion_set_port (m, GREEN_LED_MASK | WHITE_LED_MASK
                                               | BLUE_LED_MASK) < 0)
            err_exit ("%s: motion set port", name);
    }
    return m;
}

void hpad_cb (struct hpad *h, void *arg)
{
    struct prog_context *ctx = arg;
    int val;

    if ((val = hpad_read (h)) < 0) {
        err ("hpad_read");
        return;
    }

    bool fast = (val & HPAD_MASK_FAST);
    switch (val & HPAD_MASK_KEYS) {
        case HPAD_KEY_NONE: {
            int v = 0;
            if (ctx->t_tracking)
                v = controller_velocity (&ctx->opt.t, ctx->opt.t.sidereal);
            if (motion_set_velocity (ctx->t, ctx->west ? v : -1*v) < 0)
                err ("t: set velocity");
            if (motion_set_velocity (ctx->d, 0) < 0)
                err ("d: set velocity");
            break;
        }
        case HPAD_KEY_NORTH: { // DEC+
            int v = controller_velocity (&ctx->opt.d,
                                fast ? ctx->opt.d.fast : ctx->opt.d.slow);
            if (motion_set_velocity (ctx->d, v) < 0)
                err ("d: set velocity");
            break;
        }
        case HPAD_KEY_SOUTH: { // DEC-
            int v = controller_velocity (&ctx->opt.d,
                                fast ? ctx->opt.d.fast : ctx->opt.d.slow);
            if (motion_set_velocity (ctx->d, -1*v) < 0)
                err ("d: set velocity");
            break;
        }
        case HPAD_KEY_EAST: { // RA+
            int v = controller_velocity (&ctx->opt.t,
                                fast ? ctx->opt.t.fast : ctx->opt.t.slow);
            if (motion_set_velocity (ctx->t, ctx->west ? -1*v : v) < 0)
                err ("t: set velocity");
            break;
        }
        case HPAD_KEY_WEST: { // RA-
            int v = controller_velocity (&ctx->opt.t,
                                fast ? ctx->opt.t.fast : ctx->opt.t.slow);
            if (motion_set_velocity (ctx->t, ctx->west ? v : -1*v) < 0)
                err ("t: set velocity");
            break;
        }
        case (HPAD_KEY_M1 | HPAD_KEY_M2): /* not assigned*/
            break;
        case HPAD_KEY_M1: /* unused */
            break;
        case HPAD_KEY_M2: /* toggle tracking (key release updates motion) */
            ctx->t_tracking = !ctx->t_tracking;
            break;
    }
}

/* FIXME: guide/hpad will interfere with each other, e.g. if a handpad slew
 * is in progress, a GUIDE_NONE event will stop it.
 * FIXME: make guide speed configurable
 */
void guide_cb (struct guide *g, void *arg)
{
    struct prog_context *ctx = arg;
    int val;

    if ((val = guide_read (g)) < 0) {
        err ("guide_read");
        return;
    }

    if (val == GUIDE_NONE) {
        int v = 0;
        if (ctx->t_tracking)
            v = controller_velocity (&ctx->opt.t, ctx->opt.t.sidereal);
        if (motion_set_velocity (ctx->t, ctx->west ? v : -1*v) < 0)
            err ("t: set velocity");
        if (motion_set_velocity (ctx->d, 0) < 0)
            err ("d: set velocity");
    } else {
        if ((val & GUIDE_DEC_PLUS)) {
            int v = controller_velocity (&ctx->opt.d, ctx->opt.d.slow);
            if (motion_set_velocity (ctx->d, v) < 0)
                err ("d: set velocity");
        }
        else if ((val & GUIDE_DEC_MINUS)) {
            int v = controller_velocity (&ctx->opt.d, ctx->opt.d.slow);
            if (motion_set_velocity (ctx->d, -1*v) < 0)
                err ("d: set velocity");
        }
        if ((val & GUIDE_RA_PLUS)) {
            int v = controller_velocity (&ctx->opt.t, ctx->opt.t.slow);
            if (motion_set_velocity (ctx->t, ctx->west ? -1*v : v) < 0)
                err ("t: set velocity");
        }
        else if ((val & GUIDE_RA_MINUS)) {
            int v = controller_velocity (&ctx->opt.t, ctx->opt.t.slow);
            if (motion_set_velocity (ctx->t, ctx->west ? v : -1*v) < 0)
                err ("t: set velocity");
        }
    }
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
    bbox_set_position (bb, ctx->west ? t : -1*t, d);
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
    lx200_set_position (lx, ctx->west ? t_degrees : -1*t_degrees, d_degrees);
}

/* LX200 protocol notifies us that slew "button" events
 * have occurred.
 */
void lx200_slew_cb (struct lx200 *lx, void *arg)
{
    struct prog_context *ctx = arg;
    int val = lx200_get_slew (lx);

    if (val == LX200_SLEW_NONE) {
        int v = 0;
        if (ctx->t_tracking)
            v = controller_velocity (&ctx->opt.t, ctx->opt.t.sidereal);
        if (motion_set_velocity (ctx->t, ctx->west ? v : -1*v) < 0)
            err ("t: set velocity");
        if (motion_set_velocity (ctx->d, 0) < 0)
            err ("d: set velocity");
    } else {
        if ((val & LX200_SLEW_NORTH)) { //DEC+
            int v = controller_velocity (&ctx->opt.d, ctx->opt.d.fast);
            if (motion_set_velocity (ctx->d, v) < 0)
                err ("d: set velocity");
        }
        else if ((val & LX200_SLEW_SOUTH)) { // DEC-
            int v = controller_velocity (&ctx->opt.d, ctx->opt.d.fast);
            if (motion_set_velocity (ctx->d, -1*v) < 0)
                err ("d: set velocity");
        }
        if ((val & LX200_SLEW_EAST)) { // RA+
            int v = controller_velocity (&ctx->opt.t, ctx->opt.t.fast);
            if (motion_set_velocity (ctx->t, ctx->west ? -1*v : v) < 0)
                err ("t: set velocity");
        }
        else if ((val & LX200_SLEW_WEST)) { // RA-
            int v = controller_velocity (&ctx->opt.t, ctx->opt.t.fast);
            if (motion_set_velocity (ctx->t, ctx->west ? v : -1*v) < 0)
                err ("t: set velocity");
        }
    }
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

    t = t_degrees/360.0 * ctx->opt.t.steps;
    d = d_degrees/360.0 * ctx->opt.d.steps;

    if (motion_set_position (ctx->t, ctx->west ? t : -1*t) < 0)
        err ("t: set position");
    if (motion_set_position (ctx->d, d) < 0)
        err ("t: set position");
}

/* Calculate velocity in steps/sec for motion controller from degrees/sec.
 * Take into account controller velocity scaling in 'auto' mode.
 */
int controller_velocity (struct config_axis *axis, double degrees_persec)
{
    double steps_persec = degrees_persec * axis->steps / 360.;

    if (axis->mode == 1)
        steps_persec *= 1<<(axis->resolution);
    return lrint (steps_persec);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
