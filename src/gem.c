/*****************************************************************************
 *  Copyright (C) 2015 Jim Garlick
 *  Written by Jim Garlick <garlick.jim@gmail.com>
 *  All Rights Reserved.
 *
 *  This file is part of gem-controld
 *  For details, see <https://github.com/garlick/gem-controld>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/

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
    char *ra_device;
    int ra_track;
    int ra_slow;
    int ra_fast;
    char *dec_device;
    int dec_slow;
    int dec_fast;
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
    opt.ra_slow = 200;
    opt.ra_fast = 8000;
    opt.dec_slow = 200;
    opt.dec_fast = 8000;
    prog = basename (argv[0]);

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

    if (!opt.ra_device)
        opt.ra_device = xstrdup ("/dev/ttyS0");
    if (!opt.dec_device)
        opt.dec_device = xstrdup ("/dev/ttyS1");

    gpio_init ();

    /* Initialize RA
     */
    motion_t ra;
    ra = motion_init (opt.ra_device, "RA", opt.debug ? MOTION_DEBUG : 0);
    if (!ra)
        err_exit ("ra init: %s", opt.ra_device);
    if (opt.ra_track != 0) {
        if (motion_set_velocity (ra, opt.ra_track) < 0)
            err_exit ("ra set velocity: %s", opt.ra_device);
    }

    /* Initialize DEC
     */
    motion_t dec;
    dec = motion_init (opt.dec_device, "DEC", opt.debug ? MOTION_DEBUG : 0);

    /* Respond to button presses.
     */
    bool bye = false;
    while (!bye) {
        int keys = gpio_event ();
        bool fast = (keys & 0x8);
        switch (keys & 0x7) {
            case 0: /* nothing */
                if (motion_set_velocity (ra, opt.ra_track) < 0)
                    err_exit ("ra set velocity");
                if (motion_set_velocity (dec, 0) < 0)
                    err_exit ("dec set velocity");
                break;
            case 1: /* N */
                if (motion_set_velocity (dec, fast ? opt.dec_fast
                                                   : opt.dec_slow) < 0)
                    err_exit ("ra set velocity");
                break;
            case 2: /* S */
                if (motion_set_velocity (dec, fast ? -1*opt.dec_fast
                                                   : -1*opt.dec_slow) < 0)
                    err_exit ("ra set velocity");
                break;
            case 3: /* W */
                if (motion_set_velocity (ra, fast ? opt.ra_fast
                                                  : opt.ra_slow) < 0)
                    err_exit ("ra set velocity");
                break;
            case 4: /* E */
                if (motion_set_velocity (ra, fast ? -1*opt.ra_fast
                                                  : -1*opt.ra_slow) < 0)
                    err_exit ("ra set velocity");
                break;
            case 5: { /* M1 */
                double x, y;
                if (motion_get_position (ra, &x) < 0
                 || motion_get_position (dec, &y) < 0)
                    err_exit ("failed to get x,y position");
                msg ("ra=%.1f dec=%.1f", x, y);
                break;
            }
            case 6: /* M2 */
                break;
            case 7: /* M1+M2 */
                bye = true;
                break;
        }
    }

    gpio_fini ();

    motion_fini (ra); 

    return 0;
}

int config_cb (void *user, const char *section, const char *name,
               const char *value)
{
    opt_t *opt = user;

    if (!strcmp (section, "general")) {
        if (!strcmp (name, "debug")) {
            if (!strcmp (value, "yes"))
                opt->debug = true;
            else if (!strcmp (value, "no"))
                opt->debug = false;
        }
    } else if (!strcmp (section, "ra")) {
        if (!strcmp (name, "device")) {
            if (opt->ra_device)
                free (opt->ra_device);
            opt->ra_device = xstrdup (value);
        } else if (!strcmp (name, "track")) {
            opt->ra_track = strtoul (value, NULL, 10);
        } else if (!strcmp (name, "slow")) {
            opt->ra_slow = strtoul (value, NULL, 10);
        } else if (!strcmp (name, "fast")) {
            opt->ra_fast = strtoul (value, NULL, 10);
        }
    } else if (!strcmp (section, "dec")) {
        if (!strcmp (name, "device")) {
            if (opt->dec_device)
                free (opt->dec_device);
            opt->dec_device = xstrdup (value);
        } else if (!strcmp (name, "slow")) {
            opt->dec_slow = strtoul (value, NULL, 10);
        } else if (!strcmp (name, "fast")) {
            opt->dec_fast = strtoul (value, NULL, 10);
        }
    }
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
