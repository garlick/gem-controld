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

#include "point.h"
#include "log.h"

static const double sol_sid_ratio = 1.002737909350795; // solar/sidereal day

struct point {
    int flags;
    struct lnh_lnlat_posn   observer; // observer's latitude, longitude
    struct ln_equ_posn      posn_raw; // uncorrected telescope position (deg)
    struct ln_equ_posn      zpc;      // zero point correction (deg)
    struct lnh_equ_posn     target;   // ra,dec of current "target" (deg)
};

/* Get the apparent local sidereal time, in degrees,
 * derived from the system clock.
 */
static double get_lst (struct point *p)
{
    double jd = ln_get_julian_from_sys (); // UT
    double gast = ln_get_apparent_sidereal_time (jd);
    double lng_hrs = ln_dms_to_deg (&p->observer.lng) / 15;
    double lst = gast + lng_hrs*sol_sid_ratio;

    if (lst < 0)
        lst += 24;
    return lst*15;
}

void point_set_latitude (struct point *p, int deg, int min, double sec)
{
    p->observer.lat.neg = (deg < 0);
    p->observer.lat.degrees = abs (deg);
    p->observer.lat.minutes = min;
    p->observer.lat.seconds = sec;

    if ((p->flags & POINT_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_dms_to_deg (&p->observer.lat));
}

void point_set_longitude (struct point *p, int deg, int min, double sec)
{
    p->observer.lng.neg = (deg < 0);
    p->observer.lng.degrees = abs (deg);
    p->observer.lng.minutes = min;
    p->observer.lng.seconds = sec;

    if ((p->flags & POINT_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_dms_to_deg (&p->observer.lng));
}

void point_set_target_dec (struct point *p, int deg, int min, double sec)
{
    p->target.dec.neg = (deg < 0);
    p->target.dec.degrees = abs (deg);
    p->target.dec.minutes = min;
    p->target.dec.seconds = sec;

    if ((p->flags & POINT_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_dms_to_deg (&p->target.dec));
}

void point_set_target_ra (struct point *p, int hr, int min, double sec)
{
    p->target.ra.hours = hr;
    p->target.ra.minutes = min;
    p->target.ra.seconds = sec;

    if ((p->flags & POINT_DEBUG))
        msg ("%s: %.6lf", __FUNCTION__, ln_hms_to_deg (&p->target.ra));
}

void point_set_position (struct point *p, double t, double d)
{
    p->posn_raw.ra = t;
    p->posn_raw.dec = d;

    if ((p->flags & POINT_DEBUG))
        msg ("%s: raw position = (%.6lf, %.6lf)",
             __FUNCTION__, p->posn_raw.ra, p->posn_raw.dec);
}

void point_sync_target (struct point *p)
{
    double ha = get_lst (p) - ln_hms_to_deg (&p->target.ra);
    double dec = ln_dms_to_deg (&p->target.dec);

    p->zpc.ra = ha - p->posn_raw.ra;
    p->zpc.dec = dec - p->posn_raw.dec;

    if ((p->flags & POINT_DEBUG))
        msg ("%s: zero point corrections = (%.6lf, %.6lf)",
             __FUNCTION__, p->zpc.ra, p->zpc.dec);
}

void point_get_position_ra (struct point *p, int *hr, int *min, double *sec)
{
    double ha = p->posn_raw.ra + p->zpc.ra; // hour angle
    double lst = get_lst (p);               // apparent local sidereal time
    struct ln_hms ra;                       // ra = lst - ha

    ln_deg_to_hms (lst - ha, &ra);
    *hr = ra.hours;
    *min = ra.minutes;
    *sec = ra.seconds;
}

void point_get_position_dec (struct point *p, int *deg, int *min, double *sec)
{
    struct ln_dms dec;

    ln_deg_to_dms (p->posn_raw.dec + p->zpc.dec, &dec);
    *deg = dec.degrees*(dec.neg ? -1 : 1);
    *min = dec.minutes;
    *sec = dec.seconds;
}

void point_set_flags (struct point *p, int flags)
{
    p->flags = flags;
}

struct point *point_new (void)
{
    struct point *p = calloc (1, sizeof (*p));
    return p;
}

void point_destroy (struct point *p)
{
    free (p);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
