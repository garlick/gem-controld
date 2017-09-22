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

#include <stdlib.h>
#include <libnova/libnova.h>
#include <math.h>

#include "util.h"
#include "log.h"

static const double sol_sid_ratio = 1.002737909350795; // solar/sidereal day

struct util {
    int flags;
    struct ln_dms longitude;
    struct ln_dms latitude;
    struct lnh_equ_posn target;
    double t, d;                // current telescope position (deg)
    double t_offset, d_offset;  // offsets to transform t,d => lha,dec (deg)
};

/* Get the apparent local sidereal time, in degrees,
 * derived from the system clock.
 */
static double get_lst (struct util *u)
{
    double jd = ln_get_julian_from_sys (); // UT
    double gast = ln_get_apparent_sidereal_time (jd);
    double lst = gast + (ln_dms_to_deg (&u->longitude)/15)*sol_sid_ratio;

    if (lst < 0)
        lst += 24;
    return lst*15;
}

void util_set_latitude (struct util *u, int deg, int min, double sec)
{
    u->latitude.neg = (deg < 0);
    u->latitude.degrees = abs (deg);
    u->latitude.minutes = min;
    u->latitude.seconds = sec;

    if ((u->flags & UTIL_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_dms_to_deg (&u->latitude));
}

void util_set_longitude (struct util *u, int deg, int min, double sec)
{
    u->longitude.neg = (deg < 0);
    u->longitude.degrees = abs (deg);
    u->longitude.minutes = min;
    u->longitude.seconds = sec;

    if ((u->flags & UTIL_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_dms_to_deg (&u->longitude));
}

void util_set_target_dec (struct util *u, int deg, int min, double sec)
{
    u->target.dec.neg = (deg < 0);
    u->target.dec.degrees = abs (deg);
    u->target.dec.minutes = min;
    u->target.dec.seconds = sec;

    if ((u->flags & UTIL_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_dms_to_deg (&u->target.dec));
}

void util_set_target_ra (struct util *u, int hr, int min, double sec)
{
    u->target.ra.hours = hr;
    u->target.ra.minutes = min;
    u->target.ra.seconds = sec;

    if ((u->flags & UTIL_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_hms_to_deg (&u->target.ra));
}

void util_set_position (struct util *u, double t, double d)
{
    u->t = t;
    u->d = d;
}

void util_sync_target (struct util *u)
{
    double ha = get_lst (u) - ln_hms_to_deg (&u->target.ra);
    double dec = ln_dms_to_deg (&u->target.dec);

    u->t_offset = ha - u->t;
    u->d_offset = dec - u->d;
}

void util_get_position_ra (struct util *u, int *hr, int *min, double *sec)
{
    double ha = u->t + u->t_offset;         // hour angle
    double lst = get_lst (u);               // local sidereal time
    struct ln_hms ra;                       // ra = lst - ha

    if ((u->flags & UTIL_DEBUG)) {
        msg ("%s: ha=%.6f lst=%.6f ra=%.6f", __FUNCTION__, ha, lst, lst - ha);
    }

    ln_deg_to_hms (lst - ha, &ra);
    *hr = ra.hours;
    *min = ra.minutes;
    *sec = ra.seconds;
}

void util_get_position_dec (struct util *u, int *deg, int *min, double *sec)
{
    double d = u->d + u->d_offset;          // dec
    struct ln_dms dec;

    if ((u->flags & UTIL_DEBUG)) {
        msg ("%s: dec=%.6f", __FUNCTION__, d);
    }

    ln_deg_to_dms (d, &dec);
    *deg = dec.degrees*(dec.neg ? -1 : 1);
    *min = dec.minutes;
    *sec = dec.seconds;
}

void util_set_flags (struct util *u, int flags)
{
    u->flags = flags;
}

struct util *util_new (void)
{
    struct util *u = calloc (1, sizeof (*u));
    return u;
}

void util_destroy (struct util *u)
{
    free (u);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
