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
} opt_t;

char *config_filename = NULL;

int config_cb (void *user, const char *section, const char *name,
               const char *value);

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
);
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

    if (!opt.ra.device || !opt.dec.device)
        msg_exit ("You must configure ra and dec serial devices");

    /* Initialize RA
     */
    motion_t ra;
    ra = motion_init (opt.ra.device, "RA", opt.debug ? MOTION_DEBUG : 0);
    if (!ra)
        err_exit ("ra init: %s", opt.ra.device);
    if (motion_set_resolution (ra, opt.ra.resolution) < 0)
        err_exit ("ra set resolution");
    if (motion_set_current (ra, opt.ra.ihold, opt.ra.irun) < 0)
        err_exit ("ra set current: %s", opt.ra.device);
    if (motion_set_acceleration (ra, opt.ra.accel, opt.ra.decel) < 0)
        err_exit ("ra set acceleration: %s", opt.ra.device);
    if (motion_set_velocity (ra, opt.ra.track) < 0)
        err_exit ("ra set velocity: %s", opt.ra.device);

    /* Initialize DEC
     */
    motion_t dec;
    dec = motion_init (opt.dec.device, "DEC", opt.debug ? MOTION_DEBUG : 0);
    if (!dec)
        err_exit ("dec init: %s", opt.dec.device);
    if (motion_set_resolution (dec, opt.dec.resolution) < 0)
        err_exit ("dec set resolution");
    if (motion_set_current (dec, opt.dec.ihold, opt.dec.irun) < 0)
        err_exit ("dec set current: %s", opt.dec.device);
    if (motion_set_acceleration (dec, opt.dec.accel, opt.dec.decel) < 0)
        err_exit ("dec set acceleration: %s", opt.dec.device);
    if (motion_set_velocity (dec, opt.dec.track) < 0)
        err_exit ("dec set velocity: %s", opt.dec.device);

    /* Respond to button presses.
     */
    gpio_init ();
    bool bye = false;
    while (!bye) {
        int keys = gpio_event ();
        bool fast = (keys & 0x8);
        switch (keys & 0x7) {
            case 0: /* nothing */
                if (motion_set_velocity (ra, opt.ra.track) < 0)
                    err_exit ("ra set velocity");
                if (motion_set_velocity (dec, opt.dec.track) < 0)
                    err_exit ("dec set velocity");
                break;
            case 1: /* N */
                if (motion_set_velocity (dec, fast ? opt.dec.fast
                                                   : opt.dec.slow) < 0)
                    err_exit ("ra set velocity");
                break;
            case 2: /* S */
                if (motion_set_velocity (dec, fast ? -1*opt.dec.fast
                                                   : -1*opt.dec.slow) < 0)
                    err_exit ("ra set velocity");
                break;
            case 3: /* W */
                if (motion_set_velocity (ra, fast ? opt.ra.fast
                                                  : opt.ra.slow) < 0)
                    err_exit ("ra set velocity");
                break;
            case 4: /* E */
                if (motion_set_velocity (ra, fast ? -1*opt.ra.fast
                                                  : -1*opt.ra.slow) < 0)
                    err_exit ("ra set velocity");
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
    gpio_fini ();

    motion_fini (dec); 
    motion_fini (ra); 

    return 0;
}

int config_axis (opt_axis_t *a, const char *name, const char *value)
{
    if (!strcmp (name, "device")) {
        if (a->device)
            free (a->device);
        a->device = xstrdup (value);
    } else if (!strcmp (name, "resolution"))
        a->resolution = strtoul (value, NULL, 10);
    else if (!strcmp (name, "track"))
        a->track = strtoul (value, NULL, 10);
    else if (!strcmp (name, "slow"))
        a->slow = strtoul (value, NULL, 10);
    else if (!strcmp (name, "fast"))
        a->fast = strtoul (value, NULL, 10);
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
    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
