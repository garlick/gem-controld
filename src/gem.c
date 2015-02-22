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
    char *dec_device;
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

    msg ("ra_device: %s", opt.ra_device);
    msg ("ra_track: %d", opt.ra_track);
    msg ("dec_device: %s", opt.dec_device);
    msg ("debug: %s", opt.debug ? "yes" : "no");

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

    motion_t dec;
    dec = motion_init (opt.dec_device, "DEC", opt.debug ? MOTION_DEBUG : 0);

    int keys;
    for (;;) {
        keys = gpio_event ();
        msg ("gpio: 0x%x", keys);
    }

    gpio_fini ();

    //motion_fini (ra); 

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
        }
    } else if (!strcmp (section, "dec")) {
        if (!strcmp (name, "device")) {
            if (opt->dec_device)
                free (opt->dec_device);
            opt->dec_device = xstrdup (value);
        }
    }
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
