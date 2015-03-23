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
#include <zmq.h>
#include <czmq.h>

#include "libini/ini.h"
#include "libutil/log.h"
#include "libutil/xzmalloc.h"
#include "libutil/gpio.h"
#include "libutil/ev_zmq.h"

char *prog = "";

typedef struct {
    bool debug;
    char *req_uri;
} opt_t;

typedef struct {
    opt_t opt;
    zsock_t *zreq;
} ctx_t;

typedef struct {
    const char *name;
    void (*fun)(ctx_t *ctx, int ac, char **av);
} op_t;

char *config_filename = CONFIG_FILENAME;

int config_cb (void *user, const char *section, const char *name,
               const char *value);

void op_position (ctx_t *ctx, int ac, char **av);

static op_t ops[] = {
    { "position", op_position },
};

#define OPTIONS "+c:h"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: gem position\n"
"    -c,--config FILE         set path to config file\n"
);
    exit (1);
}

op_t *op_lookup (const char *name)
{
    int i;
    for (i = 0; i < sizeof (ops) / sizeof (ops[0]); i++)
        if (!strcmp (ops[i].name, name))
            return &ops[i];
    return NULL;
}

int main (int argc, char *argv[])
{
    int ch;
    ctx_t ctx;
    op_t *op;

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
    if (config_filename) {
        int rc = ini_parse (config_filename, config_cb, &ctx.opt);
        if (rc == -1) /* file open error */
            err_exit ("%s", config_filename);
        else if (rc == -2) /* out of memory */
            errn_exit (ENOMEM, "%s", config_filename);
        else if (rc > 0) /* line number */
            msg_exit ("%s: parse error on line %d", config_filename, rc);
    }

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
    if (!ctx.opt.req_uri)
        msg_exit ("no req uri was configured");
    if (optind == argc)
        usage ();
    if (!(op = op_lookup (argv[optind++])))
        usage ();

    setenv ("ZSYS_LINGER", "10", 1);
    if (!(ctx.zreq = zsock_new_dealer (ctx.opt.req_uri)))
        err_exit ("zsock_new_req %s", ctx.opt.req_uri);
    zsys_handler_set (NULL); /* disable zeromq signal handling */

    op->fun (&ctx, argc - optind, argv + optind);

    zsock_destroy (&ctx.zreq);

    return 0;
}

void op_position (ctx_t *ctx, int ac, char **av)
{
    int rc, x, y;

    if (zsock_send (ctx->zreq, "iii", 0, 0, 0) < 0) {
        err ("zstr_send");
        return;
    }
    if (zsock_recv (ctx->zreq, "iii", &rc, &x, &y) < 0) {
        err ("zstr_recv");
        return;
    }
    msg ("%d, %d, %d", rc, x, y);
}

int config_cb (void *user, const char *section, const char *name,
               const char *value)
{
    opt_t *opt = user;
    int rc = 1; /* 0 = error */

    if (!strcmp (section, "sockets")) {
        if (!strcmp (name, "req")) {
            if (opt->req_uri)
                free (opt->req_uri);
            opt->req_uri = xstrdup (value);
        } 
    }
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
