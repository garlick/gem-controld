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
#include "lx200.h"

#define OPTIONS "+c:h"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {0, 0, 0, 0},
};

void lx200_goto_cb (struct lx200 *lx, void *arg);
void lx200_slew_cb (struct lx200 *lx, void *arg);
void lx200_pos_cb (struct lx200 *lx, void *arg);

static void usage (void)
{
    fprintf (stderr,
"Usage: test-lx200 [OPTIONS]\n"
"    -c,--config FILE    set path to config file\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    char *config_filename = NULL;
    struct config cfg;
    struct lx200 *lx;
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

    if (!(loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    lx = lx200_new ();
    if (lx200_init (lx, DEFAULT_LX200_PORT, LX200_DEBUG) < 0)
        err_exit ("hpad_init");
    lx200_set_position_cb (lx, lx200_pos_cb, NULL);
    lx200_set_slew_cb (lx, lx200_slew_cb, NULL);
    lx200_set_goto_cb (lx, lx200_goto_cb, NULL);
    lx200_start (loop, lx);
    msg ("lx200 configured");

    ev_run (loop, 0);

    ev_loop_destroy (loop);

    lx200_stop (loop, lx);
    lx200_destroy (lx);

    return 0;
}

void lx200_pos_cb (struct lx200 *lx, void *arg)
{
    lx200_set_position (lx, 0., 0.);
}

void lx200_slew_cb (struct lx200 *lx, void *arg)
{
    (void)lx200_get_slew (lx);
}

void lx200_goto_cb (struct lx200 *lx, void *arg)
{
    double t, d;

    lx200_get_target (lx, &t, &d);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
