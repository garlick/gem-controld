/*****************************************************************************\
 *  Copyright (C) 2017 Jim Garlick
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
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <ev.h>

#include "log.h"
#include "xzmalloc.h"
#include "configfile.h"
#include "hpad.h"
#include "guide.h"

#define OPTIONS "+c:h"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {0, 0, 0, 0},
};

void hpad_cb (struct hpad *h, void *arg);
void guide_cb (struct guide *g, void *arg);

static void usage (void)
{
    fprintf (stderr,
"Usage: gem [OPTIONS]\n"
"    -c,--config FILE    set path to config file\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    char *config_filename = NULL;
    struct config cfg;
    struct hpad *hpad;
    struct guide *guide;
    char *prog;
    struct ev_loop *loop;

    memset (&cfg, 0, sizeof (cfg));

    prog = basename (argv[0]);
    log_init (prog);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --config FILE */
                config_filename = xstrdup (optarg);
                break;
        }
    }
    configfile_init (config_filename, &cfg);

    optind = 0;
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --config FILE (handled above) */
                break;
            case 'h':   /* --help */
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();
    if (!cfg.hpad_gpio)
        msg_exit ("hpad_gpio was not configured");
    if (!cfg.guide_gpio)
        msg_exit ("guide_gpio was not configured");

    if (!(loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    hpad = hpad_new ();
    if (hpad_init (hpad, cfg.hpad_gpio, cfg.hpad_debounce, hpad_cb, NULL) < 0)
        err_exit ("hpad_init");
    hpad_start (loop, hpad);
    msg ("hpad configured");

    guide = guide_new ();
    if (guide_init (guide, cfg.guide_gpio, cfg.guide_debounce, guide_cb, NULL) < 0)
        err_exit ("guide_init");
    guide_start (loop, guide);
    msg ("guide configured");

    ev_run (loop, 0);

    ev_loop_destroy (loop);

    guide_stop (loop, guide);
    guide_destroy (guide);

    hpad_stop (loop, hpad);
    hpad_destroy (hpad);

    return 0;
}

void hpad_cb (struct hpad *h, void *arg)
{
    int val;

    if ((val = hpad_read (h)) < 0) {
        err ("hpad");
        return;
    }

    bool fast = (val & HPAD_MASK_FAST);
    switch (val & HPAD_MASK_KEYS) {
        case HPAD_KEY_NONE: {
            msg ("hpad: KEY_NONE (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
        }
        case HPAD_KEY_NORTH: {
            msg ("hpad: KEY_NORTH (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
        }
        case HPAD_KEY_SOUTH: {
            msg ("hpad: KEY_SOUTH (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
        }
        case HPAD_KEY_WEST: {
            msg ("hpad: KEY_WEST (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
        }
        case HPAD_KEY_EAST: {
            msg ("hpad: KEY_EAST (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
        }
        case (HPAD_KEY_M1 | HPAD_KEY_M2): /* zero */
            msg ("hpad: KEY_M1 and KEY_M2 (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
        case HPAD_KEY_M1: /* unused */
            msg ("hpad: KEY_M1 (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
        case HPAD_KEY_M2: /* toggle stop */
            msg ("hpad: KEY_M2 (0x%x) fast=%s", val, fast ? "yes" : "no");
            break;
    }
}

void guide_cb (struct guide *g, void *arg)
{
    int val;

    if ((val = guide_read (g)) < 0) {
        err ("guide");
        return;
    }
    msg ("guide: (0x%x) %sRA+ %sRA- %sDEC+ %sDEC-", val,
         (val & GUIDE_RA_PLUS) ? "*" : " ",
         (val & GUIDE_RA_MINUS) ? "*" : " ",
         (val & GUIDE_DEC_PLUS) ? "*" : " ",
         (val & GUIDE_DEC_MINUS) ? "*" : " ");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
