#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <string.h>
#include <ev.h>

#include "motion.h"

char *prog = "";

const char *ra_device = "/dev/tty01";
const char *dec_device = "/dev/tty02";

int main (int argc, char *argv[])
{
    struct ev_loop *loop = EV_DEFAULT;
    int ra = -1;
    int dec = -1;

    prog = basename (argv[0]);

    ra = motion_init (ra_device);
    if (ra < 0) {
        fprintf (stderr, "%s: %s: %s\n", prog, ra_device, strerror (errno));
        exit (1);
    }
    dec = motion_init (dec_device);
    if (dec < 0) {
        fprintf (stderr, "%s: %s: %s\n", prog, dec_device, strerror (errno));
        exit (1);
    }

    while (ev_run (loop, 0))
        ;

    close (ra);
    close (dec);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
