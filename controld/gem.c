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
    bool no_motion;
    char *hpad_gpio;
    double hpad_debounce;
    char *req_uri;
} opt_t;

typedef struct {
    opt_t opt;
    hpad_t hpad;
    motion_t ra, dec;
    zsock_t *zreq;
    ev_zmq req_watcher;
} ctx_t;

char *config_filename = CONFIG_FILENAME;

int config_cb (void *user, const char *section, const char *name,
               const char *value);
void hpad_cb (hpad_t h, void *arg);
motion_t init_axis (opt_axis_t *a, const char *name, bool debug);


void zreq_cb (struct ev_loop *loop, ev_zmq *w, int revents);


#define OPTIONS "+c:hdn"
static const struct option longopts[] = {
    {"config",               required_argument, 0, 'c'},
    {"help",                 no_argument,       0, 'h'},
    {"debug",                no_argument,       0, 'd'},
    {"no-motion",            no_argument,       0, 'n'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: gem [OPTIONS]\n"
"    -c,--config FILE         set path to config file\n"
"    -d,--debug               emit verbose debugging to stderr\n"
"    -n,--no-motion           do not attempt to talk to motion controllers\n"
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
            case 'd':   /* --debug */
                ctx.opt.debug = true;
                break;
            case 'n':   /* --no-motion */
                ctx.opt.no_motion = true;
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
        ctx.ra = init_axis (&ctx.opt.ra, "RA", ctx.opt.debug);
        ctx.dec = init_axis (&ctx.opt.dec, "DEC", ctx.opt.debug);
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
    zframe_t *sender = NULL;
    int op, x, y;
    int errnum;
    int rc = -1;

    if (!(sender = zframe_recv (ctx->zreq))) {
        err ("zframe_recv");
        goto done_noreply;
    }
    if (zsock_recv (ctx->zreq, "iii", &op, &x, &y) < 0) {
        err ("zsock_recv");
        goto done_noreply;
    }
    switch (op) {
        case 0: { /* position */
            double ra, dec;
            if (motion_get_position (ctx->ra, &ra) < 0)
                goto done;
            if (motion_get_position (ctx->dec, &dec) < 0)
                goto done;
            x = (int)ra;
            y = (int)dec;
            rc = 0;
            break;
        }
        case 1: { /* stop */
            if (motion_set_velocity (ctx->ra, 0) < 0)
                goto done;
            if (motion_set_velocity (ctx->dec, 0) < 0)
                goto done;
            x = 0;
            y = 0;
            rc = 0;
            break;
        }
        case 2: { /* track */
            if (motion_set_velocity (ctx->ra, ctx->opt.ra.track) < 0)
                goto done;
            if (motion_set_velocity (ctx->dec, ctx->opt.dec.track) < 0)
                goto done;
            x = ctx->opt.ra.track;
            y = ctx->opt.dec.track;
            rc = 0;
            break;
        }
        case 3: { /* track [args] */
            if (motion_set_velocity (ctx->ra, x) < 0)
                goto done;
            if (motion_set_velocity (ctx->dec, y) < 0)
                goto done;
            rc = 0;
            break;
        }
        case 4: { /* zero */
            if (motion_set_origin (ctx->ra) < 0)
                goto done;
            if (motion_set_origin (ctx->dec) < 0)
                goto done;
            x = 0;
            y = 0;
            rc = 0;
            break;
        }
    }
done:
    errnum = errno;
    if (zframe_send (&sender, ctx->zreq, ZFRAME_MORE) < 0) {
        err ("zframe_send");
        goto done_noreply;
    }
    if (zsock_send (ctx->zreq, "iii", rc, rc < 0 ? errnum : x, y) < 0) {
        err ("zsock_send");
        goto done_noreply;
    }
done_noreply:
    zframe_destroy (&sender);
    return;
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
    int rc = 1; /* 0 = error */
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
    return rc;
}

int config_cb (void *user, const char *section, const char *name,
               const char *value)
{
    opt_t *opt = user;
    int rc = 1; /* 0 = error */

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
            opt->hpad_gpio = xstrdup (value);
        } else if (!strcmp (name, "debounce"))
            opt->hpad_debounce = strtod (value, NULL);
    } else if (!strcmp (section, "sockets")) {
        if (!strcmp (name, "req")) {
            if (opt->req_uri)
                free (opt->req_uri);
            opt->req_uri = xstrdup (value);
        } 
    }
    return rc;
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
