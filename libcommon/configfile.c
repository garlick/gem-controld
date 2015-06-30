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

#include <stdbool.h>
#include <stdlib.h>

#include "libutil/xzmalloc.h"
#include "libutil/log.h"
#include "libini/ini.h"
#include "configfile.h"

static int config_cb (void *user, const char *section, const char *name,
                      const char *value);

void configfile_init (const char *filename, opt_t *opt)
{
    if (!filename)
        filename = CONFIG_FILENAME;
    int rc = ini_parse (filename, config_cb, opt);
    if (rc == -1) /* file open error */
        err_exit ("%s", filename);
    else if (rc == -2) /* out of memory */
        errn_exit (ENOMEM, "%s", filename);
    else if (rc > 0) /* line number */
        msg_exit ("%s::%d: parse error", filename, rc);
};

static int config_axis (opt_axis_t *a, const char *name, const char *value)
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
        a->slow = strtod (value, NULL);
    else if (!strcmp (name, "fast"))
        a->fast = strtod (value, NULL);
    else if (!strcmp (name, "ihold"))
        a->ihold = strtoul (value, NULL, 10);
    else if (!strcmp (name, "irun"))
        a->irun= strtoul (value, NULL, 10);
    else if (!strcmp (name, "accel"))
        a->accel = strtoul (value, NULL, 10);
    else if (!strcmp (name, "decel"))
        a->decel = strtoul (value, NULL, 10);
    else if (!strcmp (name, "steps"))
        a->steps = strtoul (value, NULL, 10);
    else if (!strcmp (name, "offset"))
        a->offset = strtol (value, NULL, 10);
    else if (!strcmp (name, "park"))
        a->park = strtol (value, NULL, 10);
    else if (!strcmp (name, "low_limit"))
        a->low_limit = strtol (value, NULL, 10);
    else if (!strcmp (name, "high_limit"))
        a->high_limit = strtol (value, NULL, 10);
    return rc;
}

static int config_cb (void *user, const char *section, const char *name,
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
    } else if (!strcmp (section, "t_axis"))
        rc = config_axis (&opt->t, name, value);
    else if (!strcmp (section, "d_axis"))
        rc = config_axis (&opt->d, name, value);
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
        } else if (!strcmp (name, "pub")) {
            if (opt->pub_uri)
                free (opt->pub_uri);
            opt->pub_uri = xstrdup (value);
        }
    }
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
