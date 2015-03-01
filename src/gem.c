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

#include "motion.h"
#include "gpio.h"

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
    int gpio[4];
} opt_t;

char *config_filename = NULL;

int config_cb (void *user, const char *section, const char *name,
               const char *value);
motion_t axis_init (opt_axis_t *a, const char *name, bool debug);
void axis_slew (motion_t m, opt_axis_t *a, bool fast, bool reverse);
void axis_track (motion_t m, opt_axis_t *a);

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
    //struct ev_loop *loop = EV_DEFAULT;
    int ch;
    opt_t opt;

    memset (&opt, 0, sizeof (opt));

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
    (void)ini_parse (config_filename, config_cb, &opt);

    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --config FILE (handled above) */
                break;
            case 'd':   /* --debug */
                opt.debug = true;
                break;
            case 'h':   /* --help */
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();

    motion_t ra = axis_init (&opt.ra, "RA", opt.debug);
    motion_t dec = axis_init (&opt.dec, "DEC", opt.debug);

    /* Respond to button presses.
     */
    gpio_t g = gpio_init (opt.gpio, 4);
    bool bye = false;
    while (!bye) {
        int keys = gpio_event (g);
        bool fast = (keys & 0x8);
        switch (keys & 0x7) {
            case 0: /* nothing */
                axis_track (ra, &opt.ra);
                axis_track (dec, &opt.dec);
                break;
            case 1: /* N */
                axis_slew (dec, &opt.dec, fast, false);
                break;
            case 2: /* S */
                axis_slew (dec, &opt.dec, fast, true);
                break;
            case 3: /* W */
                axis_slew (ra, &opt.ra, fast, false);
                break;
            case 4: /* E */
                axis_slew (ra, &opt.ra, fast, true);
                break;
            case 5: { /* M1 */
                double x, y;
                if (motion_get_position (ra, &x) < 0
                 || motion_get_position (dec, &y) < 0)
                    err_exit ("failed to get x,y position");
                msg ("(%.0f,%.0f)", x, y);
                break;
            }
            case 6: /* M2 */
                break;
            case 7: /* M1+M2 */
                //bye = true;
                break;
        }
    }
    gpio_fini (g);

    motion_fini (dec); 
    motion_fini (ra); 

    return 0;
}

void axis_slew (motion_t m, opt_axis_t *a, bool fast, bool reverse)
{
    const char *name = motion_name (m);
    int velocity = (fast ? a->fast : a->slow) * (reverse ? -1 : 1);

    if (motion_set_velocity (m, velocity) < 0)
        err_exit ("%s: set velocity", name);
}

void axis_track (motion_t m, opt_axis_t *a)
{
    const char *name = motion_name (m);

    if (motion_set_velocity (m, a->track) < 0)
        err_exit ("%s: set velocity", name);
}

motion_t axis_init (opt_axis_t *a, const char *name, bool debug)
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
    axis_track (m, a);
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
    else if (!strcmp (section, "gpio")) {
        if (!strcmp (name, "bit0"))
            opt->gpio[0] = strtoul (value, NULL, 10);
        else if (!strcmp (name, "bit1"))
            opt->gpio[1] = strtoul (value, NULL, 10);
        else if (!strcmp (name, "bit2"))
            opt->gpio[2] = strtoul (value, NULL, 10);
        else if (!strcmp (name, "bit3"))
            opt->gpio[3] = strtoul (value, NULL, 10);
    }
    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
