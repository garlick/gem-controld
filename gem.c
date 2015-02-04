#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <string.h>
#include <ev.h>

#include "motion.h"

char *prog = "";

const char *ra_device = "/dev/ttyO1";
const char *dec_device = "/dev/ttyO2";

const int sidereal = 30;

int main (int argc, char *argv[])
{
    struct ev_loop *loop = EV_DEFAULT;
    motion_t ra = NULL;
    motion_t dec = NULL;

    prog = basename (argv[0]);

    ra = motion_init (ra_device, "RA", 0);
    if (!ra) {
        fprintf (stderr, "%s: %s: %s\n", prog, ra_device, strerror (errno));
        exit (1);
    }
    dec = motion_init (dec_device, "DEC", 0);
    if (!dec) {
        fprintf (stderr, "%s: %s: %s\n", prog, dec_device, strerror (errno));
        exit (1);
    }

#if 0
    /* Start racking in RA
     */
    if (motion_set_velocity (ra, sidereal) < 0) {
        fprintf (stderr, "%s: RA: %s\n", prog, strerror (errno));
        exit (1);
    }
#endif

    /* Slew back and forth in dec and ra, polling for each slew completion.
     */
    double pos, ra_cur, dec_cur;
    uint8_t ra_status;
    uint8_t dec_status;
    for (pos = +8000; 1; pos *= -1) {
        if (motion_set_position (dec, pos) < 0) {
            fprintf (stderr, "%s: DEC: %s\n", prog, strerror (errno));
            exit (1);
        }
        if (motion_set_position (ra, pos) < 0) {
            fprintf (stderr, "%s: RA: %s\n", prog, strerror (errno));
            exit (1);
        }
        do {
            if (motion_get_status (dec, &dec_status) < 0) {
                fprintf (stderr, "%s: DEC: %s\n", prog, strerror (errno));
                exit (1);
            }
            if (motion_get_status (ra, &ra_status) < 0) {
                fprintf (stderr, "%s: RA: %s\n", prog, strerror (errno));
                exit (1);
            }
            if (motion_get_position (dec, &dec_cur) < 0) {
                fprintf (stderr, "%s: DEC: %s\n", prog, strerror (errno));
                exit (1);
            }
            if (motion_get_position (ra, &ra_cur) < 0) {
                fprintf (stderr, "%s: RA: %s\n", prog, strerror (errno));
                exit (1);
            }
            fprintf (stderr, "DEC: %.2f RA: %.2f\n", dec_cur, ra_cur);
            if (ra_status != 0 || dec_status != 0)
                sleep (1);
        } while (ra_status != 0 || dec_status != 0);
    }

    ev_run (loop, 0);

    motion_fini (ra);
    motion_fini (dec);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
