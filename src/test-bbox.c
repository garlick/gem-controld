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
#include "bbox.h"

#define OPTIONS "+c:h"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {0, 0, 0, 0},
};

void bbox_cb (struct bbox *bb, void *arg);

static void usage (void)
{
    fprintf (stderr,
"Usage: test-bbox [OPTIONS]\n"
"    -c,--config FILE    set path to config file\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    char *config_filename = NULL;
    struct config cfg;
    struct bbox *bb;
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

    bb = bbox_new ();
    if (bbox_init (bb, DEFAULT_BBOX_PORT, bbox_cb, NULL, BBOX_DEBUG) < 0)
        err_exit ("hpad_init");
    bbox_start (loop, bb);
    bbox_set_resolution (bb, 8192, 4096);
    msg ("bbox configured");

    ev_run (loop, 0);

    ev_loop_destroy (loop);

    bbox_stop (loop, bb);
    bbox_destroy (bb);

    return 0;
}

void bbox_cb (struct bbox *bb, void *arg)
{
    bbox_set_position (bb, 42, 84);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
