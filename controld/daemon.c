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

#include "motion.h"
#include "hpad.h"

char *prog = "";

typedef struct {
    opt_t opt;
    hpad_t hpad;
    motion_t ra, dec;
    zsock_t *zreq;
    ev_zmq req_watcher;
    bool stopped;
} ctx_t;

void hpad_cb (hpad_t h, void *arg);

motion_t init_axis (opt_axis_t *a, const char *name, int flags);

void zreq_cb (struct ev_loop *loop, ev_zmq *w, int revents);


#define OPTIONS "+c:hdns"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {"debug",                no_argument,       0, 'd'},
    {"no-motion",            no_argument,       0, 'n'},
    {"soft-init",            no_argument,       0, 's'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: gem [OPTIONS]\n"
"    -c,--config FILE         set path to config file\n"
"    -d,--debug               emit verbose debugging to stderr\n"
"    -n,--no-motion           do not attempt to talk to motion controllers\n"
"    -s,--soft-init           do not initialize/zero motion controllers\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    struct ev_loop *loop;
    int ch;
    ctx_t ctx;
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
            case 'd':   /* --debug */
                ctx.opt.debug = true;
                break;
            case 'n':   /* --no-motion */
                ctx.opt.no_motion = true;
                break;
            case 's':   /* --soft-init */
                ctx.opt.soft_init = true;
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
    if (!ctx.opt.req_uri)
        msg_exit ("no req uri was configured");

    if (!(loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    if (!ctx.opt.no_motion) {
        int flags = 0;
        if (ctx.opt.debug)
            flags |= MOTION_DEBUG;
        if (ctx.opt.soft_init)
            flags |= MOTION_SOFTINIT;
        ctx.ra = init_axis (&ctx.opt.ra, "RA", flags);
        ctx.dec = init_axis (&ctx.opt.dec, "DEC", flags);
    }

    ctx.hpad = hpad_new ();
    if (hpad_init (ctx.hpad, ctx.opt.hpad_gpio, ctx.opt.hpad_debounce,
                   hpad_cb, &ctx) < 0)
        err_exit ("hpad_init");
    hpad_start (loop, ctx.hpad);

    setenv ("ZSYS_LINGER", "10", 1);
    if (!(ctx.zreq = zsock_new_router (ctx.opt.req_uri)))
        err_exit ("zsock_new_router %s", ctx.opt.req_uri);
    if (ev_zmq_init (&ctx.req_watcher, zreq_cb,
                     zsock_resolve (ctx.zreq), EV_READ) < 0)
        err_exit ("ev_zmq_init");
    ev_zmq_start (loop, &ctx.req_watcher);
    zsys_handler_set (NULL); /* disable zeromq signal handling */

    ev_run (loop, 0); 
    ev_loop_destroy (loop);

    hpad_stop (loop, ctx.hpad);
    hpad_destroy (ctx.hpad);

    if (ctx.dec)
        motion_fini (ctx.dec); 
    if (ctx.ra)
        motion_fini (ctx.ra); 

    zsock_destroy (&ctx.zreq);

    return 0;
}

void zreq_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    ctx_t *ctx = (ctx_t *)((char *)w - offsetof (ctx_t, req_watcher));
    int rc = -1;
    gmsg_t g;
    uint8_t op;

    if (!(g = gmsg_recv (ctx->zreq))) {
        err ("gmsg_recv");
        goto done_noreply;
    }

    if (ctx->opt.debug)
        gmsg_dump (stderr, g, "recv");
    if (gmsg_get_op (g, &op) < 0)
        goto done;
    switch (op) {
        case OP_POSITION: {
            double ra, dec;
            if (motion_get_position (ctx->ra, &ra) < 0)
                goto done;
            if (motion_get_position (ctx->dec, &dec) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            if (gmsg_set_arg1 (g, (int32_t)ra) < 0)
                goto done;
            if (gmsg_set_arg2 (g, (int32_t)dec) < 0)
                goto done;
            rc = 0;
            break;
        }
        case OP_STOP: {
            if (motion_set_velocity (ctx->ra, 0) < 0)
                goto done;
            if (motion_set_velocity (ctx->dec, 0) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_TRACK: {
            uint32_t flags;
            int32_t x = ctx->opt.ra.track;
            int32_t y = ctx->opt.dec.track;
            if (gmsg_get_flags (g, &flags) < 0)
                goto done;
            if ((flags & FLAG_ARG1) && gmsg_get_arg1 (g, &x) < 0)
                goto done;
            if ((flags & FLAG_ARG2) && gmsg_get_arg2 (g, &y) < 0)
                goto done;
            if (motion_set_velocity (ctx->ra, x) < 0)
                goto done;
            if (motion_set_velocity (ctx->dec, y) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            if (gmsg_set_arg1 (g, x) < 0)
                goto done;
            if (gmsg_set_arg2 (g, y) < 0)
                goto done;
            ctx->stopped = false;
            rc = 0;
            break;
        }
        case OP_ORIGIN: {
            if (motion_set_origin (ctx->ra) < 0)
                goto done;
            if (motion_set_origin (ctx->dec) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_GOTO: {
            int32_t x, y;
            if (gmsg_get_arg1 (g, &x) < 0)
                goto done;
            if (gmsg_get_arg2 (g, &y) < 0)
                goto done;
            if (motion_set_position (ctx->ra, (double)x) < 0)
                goto done;
            if (motion_set_position (ctx->dec, (double)y) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_PARK: { /* FIXME: make park position configurable */
            if (motion_set_position (ctx->ra, 0) < 0)
                goto done;
            if (motion_set_position (ctx->dec, 0) < 0)
                goto done;
            if (gmsg_set_flags (g, 0) < 0)
                goto done;
            ctx->stopped = true;
            rc = 0;
            break;
        }
        case OP_STEPS: { /* FIXME not implemented yet */
#if 0
            if (gmsg_set_arg1 (g, ctx->opt.ra.steps) < 0)
                goto done;
            if (gmsg_set_arg2 (g, ctx->opt.dec.steps) < 0)
                goto done;
            rc = 0;
#else
            errno = ENOSYS;
            goto done;
#endif
        }
    }
done:
    if (rc != 0 && gmsg_set_error (g, errno) < 0) {
        err ("gmsg_set_error");
        goto done_noreply;
    }
    if (ctx->opt.debug)
        gmsg_dump (stderr, g, "send");
    if (gmsg_send (ctx->zreq, g) < 0) {
        err ("gmsg_send");
        goto done_noreply;
    }
done_noreply:
    gmsg_destroy (&g);
    return;
}

motion_t init_axis (opt_axis_t *a, const char *name, int flags)
{
    motion_t m;
    if (!a->device)
        msg_exit ("%s: no serial device configured", name);
    if (!(m = motion_init (a->device, name, flags)))
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

void hpad_cb (hpad_t h, void *arg)
{
    ctx_t *ctx = arg;
    int val;

    if ((val = hpad_read (h)) < 0)
        err_exit ("hpad");
    if (ctx->opt.debug)
        msg ("hpad: %d", val);
    if (ctx->opt.no_motion)
        return;

    bool fast = (val & HPAD_MASK_FAST);
    switch (val & HPAD_MASK_KEYS) {
        case HPAD_KEY_NONE: {
            int x = ctx->stopped ? 0 : ctx->opt.ra.track;
            int y = ctx->stopped ? 0 : ctx->opt.dec.track;
            if (motion_set_velocity (ctx->ra, x) < 0)
                err_exit ("ra: set velocity");
            if (motion_set_velocity (ctx->dec, y) < 0)
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
        case HPAD_KEY_M1: /* zero */
            if (motion_set_origin (ctx->ra) < 0)
                err_exit ("ra: set origin");
            if (motion_set_origin (ctx->dec) < 0)
                err_exit ("dec: set origin");
            ctx->stopped = true;
            break;
        case HPAD_KEY_M2: /* toggle stop */
            ctx->stopped = !ctx->stopped;
            break;
    }
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
