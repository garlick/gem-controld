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

#include "libutil/log.h"
#include "libutil/xzmalloc.h"
#include "libutil/gpio.h"
#include "libutil/ev_zmq.h"
#include "libcommon/configfile.h"
#include "libcommon/gmsg.h"

char *prog = "";

typedef struct {
    opt_t opt;
    zsock_t *zreq;
} ctx_t;

typedef struct {
    const char *name;
    void (*fun)(ctx_t *ctx, int ac, char **av);
} op_t;

void op_position (ctx_t *ctx, int ac, char **av);
void op_stop (ctx_t *ctx, int ac, char **av);
void op_track (ctx_t *ctx, int ac, char **av);
void op_zero (ctx_t *ctx, int ac, char **av);
void op_goto (ctx_t *ctx, int ac, char **av);
void op_park (ctx_t *ctx, int ac, char **av);

static op_t ops[] = {
    { "position", op_position },
    { "stop",     op_stop},
    { "track",    op_track},
    { "zero",     op_zero},
    { "goto",     op_goto},
    { "park",     op_park},
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
"Usage: gem [OPTIONS] position\n"
"                     stop\n"
"                     track [x y]\n"
"                     zero\n"
"                     park\n"
"OPTIONS:\n"
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
    char *config_filename = NULL;

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
    int32_t x, y;
    gmsg_t g = NULL;

    if (ac != 0) {
        msg ("position takes no arguments");
        goto done;
    }
    if (!(g = gmsg_create (OP_POSITION))) {
        err ("gmsg_create");
        goto done;;
    }
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done;
    }
    gmsg_destroy (&g);
    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done;
    }
    if (gmsg_error (g) < 0 || gmsg_get_arg1 (g, &x) < 0
                           || gmsg_get_arg2 (g, &y) < 0) {
        err ("server error");
        goto done;
    }
    msg ("%d, %d", x, y);
done:
    gmsg_destroy (&g);
}

void op_stop (ctx_t *ctx, int ac, char **av)
{
    gmsg_t g = NULL;

    if (ac != 0) {
        msg ("stop takes no arguments");
        goto done;
    }
    if (!(g = gmsg_create (OP_STOP))) {
        err ("gmsg_create");
        goto done;;
    }
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done;
    }
    gmsg_destroy (&g);
    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done;
    }
    if (gmsg_error (g) < 0) {
        err ("server error");
        goto done;
    }
    msg ("stopped");
done:
    gmsg_destroy (&g);
}

void op_track (ctx_t *ctx, int ac, char **av)
{
    int32_t x, y;
    gmsg_t g = NULL;

    if (ac > 2) {
        msg ("track takes 0 or 2 arguments");
        goto done;
    }
    if (!(g = gmsg_create (OP_TRACK))) {
        err ("gmsg_create");
        goto done;;
    }
    if (ac == 2) {
        x = strtol (av[0], NULL, 10);
        y = strtol (av[1], NULL, 10);
        if (gmsg_set_arg1 (g, x) < 0)
            err ("gmsg_set_arg1");
        if (gmsg_set_arg2 (g, y) < 0)
            err ("gmsg_set_arg2");
    }
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done;
    }
    gmsg_destroy (&g);
    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done;
    }
    if (gmsg_error (g) < 0 || gmsg_get_arg1 (g, &x) < 0
                           || gmsg_get_arg2 (g, &y) < 0) {
        err ("server error");
        goto done;
    }
    msg ("%d, %d", x, y);
done:
    gmsg_destroy (&g);
}

void op_zero (ctx_t *ctx, int ac, char **av)
{
    gmsg_t g = NULL;

    if (ac != 0) {
        msg ("zero takes no arguments");
        goto done;
    }
    if (!(g = gmsg_create (OP_ORIGIN))) {
        err ("gmsg_create");
        goto done;;
    }
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done;
    }
    gmsg_destroy (&g);
    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done;
    }
    if (gmsg_error (g) < 0) {
        err ("server error");
        goto done;
    }
    msg ("origin set");
done:
    gmsg_destroy (&g);
}

void op_goto (ctx_t *ctx, int ac, char **av)
{
    int32_t x, y;
    gmsg_t g = NULL;

    if (ac != 2) {
        msg ("goto takes 2 arguments");
        goto done;
    }
    if (!(g = gmsg_create (OP_GOTO))) {
        err ("gmsg_create");
        goto done;;
    }
    x = strtol (av[0], NULL, 10);
    y = strtol (av[1], NULL, 10);
    if (gmsg_set_arg1 (g, x) < 0)
        err ("gmsg_set_arg1");
    if (gmsg_set_arg2 (g, y) < 0)
        err ("gmsg_set_arg2");
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done;
    }
    gmsg_destroy (&g);
    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done;
    }
    if (gmsg_error (g) < 0) {
        err ("server error");
        goto done;
    }
    msg ("slewing to %d, %d", x, y);
done:
    gmsg_destroy (&g);
}

void op_park (ctx_t *ctx, int ac, char **av)
{
    gmsg_t g = NULL;

    if (ac != 0) {
        msg ("park takes no arguments");
        goto done;
    }
    if (!(g = gmsg_create (OP_PARK))) {
        err ("gmsg_create");
        goto done;;
    }
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done;
    }
    gmsg_destroy (&g);
    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done;
    }
    if (gmsg_error (g) < 0) {
        err ("server error");
        goto done;
    }
    msg ("parking");
done:
    gmsg_destroy (&g);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
