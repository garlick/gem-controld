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

#include "libini/ini.h"
#include "libutil/log.h"
#include "libutil/xzmalloc.h"
#include "libutil/gpio.h"

#include "motion.h"
#include "hpad.h"

char *prog = "";

typedef struct {
    char *device;
    int mode;
    int resolution;
    int track;
    int slow;
    int fast;
    int ihold;
    int irun;
    int accel;
    int decel;
} opt_axis_t;

typedef struct {
    opt_axis_t ra;
    opt_axis_t dec;
    bool debug;
    char *hpad_gpio;
    int hpad_debounce;
} opt_t;

typedef struct {
    opt_t opt;
    hpad_t hpad;
    motion_t ra, dec;    
} ctx_t;

char *config_filename = NULL;

int config_cb (void *user, const char *section, const char *name,
               const char *value);
void hpad_cb (hpad_t h, int val, void *arg);
motion_t init_axis (opt_axis_t *a, const char *name, bool debug);

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

    if (!config_filename) {
        struct passwd *pw = getpwuid (getuid ());
        if (!pw)
            msg_exit ("Who are you?");
        config_filename = xasprintf ("%s/.gem/config.ini", pw->pw_dir);
    }
    (void)ini_parse (config_filename, config_cb, &ctx.opt);

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

    if (!(loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    ctx.ra = init_axis (&ctx.opt.ra, "RA", ctx.opt.debug);
    ctx.dec = init_axis (&ctx.opt.dec, "DEC", ctx.opt.debug);

    ctx.hpad = hpad_new ();
    if (hpad_init (ctx.hpad, ctx.opt.hpad_gpio, ctx.opt.hpad_debounce,
                   hpad_cb, &ctx) < 0)
        err_exit ("hpad_init");
    if (hpad_start (loop, ctx.hpad) < 0)
        err_exit ("hpad_start");

    ev_run (loop, 0); 
    ev_loop_destroy (loop);

    hpad_destroy (ctx.hpad);

    motion_fini (ctx.dec); 
    motion_fini (ctx.ra); 

    return 0;
}

motion_t init_axis (opt_axis_t *a, const char *name, bool debug)
{
    motion_t m;
    if (!a->device)
        msg_exit ("%s: no serial device configured", name);
    if (!(m = motion_init (a->device, name, debug ? MOTION_DEBUG : 0)))
        err_exit ("%s: init %s", name, a->device);
    if (motion_set_current (m, a->ihold, a->irun) < 0)
        err_exit ("%s: set current", name);
    if (motion_set_mode (m, a->mode) < 0)
        err_exit ("%s: set mode", name);
    if (motion_set_resolution (m, a->resolution) < 0)
        err_exit ("%s: set resolution", name);
    if (motion_set_acceleration (m, a->accel, a->decel) < 0)
        err_exit ("%s: set acceleration", name);
    if (motion_set_velocity (m, a->track) < 0)
        err_exit ("%s: set velocity", name);
    return m;
}

int config_axis (opt_axis_t *a, const char *name, const char *value)
{
    if (!strcmp (name, "device")) {
        if (a->device)
            free (a->device);
        a->device = xstrdup (value);
    } else if (!strcmp (name, "resolution"))
        a->resolution = strtoul (value, NULL, 10);
    else if (!strcmp (name, "mode"))
        a->mode = !strcmp (value, "auto") ? 1 : 0;
    else if (!strcmp (name, "slow"))
        a->slow = strtoul (value, NULL, 10);
    else if (!strcmp (name, "fast"))
        a->fast = strtoul (value, NULL, 10);
    else if (!strcmp (name, "track"))
        a->track = strtoul (value, NULL, 10);
    else if (!strcmp (name, "ihold"))
        a->ihold = strtoul (value, NULL, 10);
    else if (!strcmp (name, "irun"))
        a->irun= strtoul (value, NULL, 10);
    else if (!strcmp (name, "accel"))
        a->accel = strtoul (value, NULL, 10);
    else if (!strcmp (name, "decel"))
        a->decel = strtoul (value, NULL, 10);
    return 0;
}

int config_cb (void *user, const char *section, const char *name,
               const char *value)
{
    opt_t *opt = user;
    int rc = 0;

    if (!strcmp (section, "general")) {
        if (!strcmp (name, "debug")) {
            if (!strcmp (value, "yes"))
                opt->debug = true;
            else if (!strcmp (value, "no"))
                opt->debug = false;
        }
    } else if (!strcmp (section, "ra"))
        rc = config_axis (&opt->ra, name, value);
    else if (!strcmp (section, "dec"))
        rc = config_axis (&opt->dec, name, value);
    else if (!strcmp (section, "hpad")) {
        if (!strcmp (name, "gpio")) {
            if (opt->hpad_gpio)
                free (opt->hpad_gpio);
            opt->hpad_gpio = strdup (value);
        } else if (!strcmp (name, "debounce"))
            opt->hpad_debounce = strtoul (value, NULL, 10);
    }
    return rc;
}

void hpad_cb (hpad_t h, int val, void *arg)
{
    ctx_t *ctx = arg;
    bool fast = (val & HPAD_MASK_FAST);

    switch (val & HPAD_MASK_KEYS) {
        case HPAD_KEY_NONE: {
            if (motion_set_velocity (ctx->ra, ctx->opt.ra.track) < 0)
                err_exit ("ra: set velocity");
            if (motion_set_velocity (ctx->dec, ctx->opt.dec.track) < 0)
                err_exit ("dec: set velocity");
            break;
        }
        case HPAD_KEY_NORTH: {
            int v = (fast ? ctx->opt.dec.fast : ctx->opt.dec.slow);
            if (motion_set_velocity (ctx->dec, v) < 0)
                err_exit ("dec: set velocity");
            break;
        }
        case HPAD_KEY_SOUTH: {
            int v = -1 * (fast ? ctx->opt.dec.fast : ctx->opt.dec.slow);
            if (motion_set_velocity (ctx->dec, v) < 0)
                err_exit ("dec: set velocity");
            break;
        }
        case HPAD_KEY_WEST: {
            int v = (fast ? ctx->opt.ra.fast : ctx->opt.ra.slow);
            if (motion_set_velocity (ctx->ra, v) < 0)
                err_exit ("ra: set velocity");
            break;
        }
        case HPAD_KEY_EAST: {
            int v = -1 * (fast ? ctx->opt.ra.fast : ctx->opt.ra.slow);
            if (motion_set_velocity (ctx->ra, v) < 0)
                err_exit ("ra: set velocity");
            break;
        } 
        case HPAD_KEY_M1:
        case HPAD_KEY_M2:
            break;
    }
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
