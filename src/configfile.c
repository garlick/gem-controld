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
#include <string.h>

#include "libini/ini.h"

#include "xzmalloc.h"
#include "log.h"
#include "configfile.h"

static int config_cb (void *user, const char *section, const char *name,
                      const char *value);

void configfile_init (const char *filename, struct config *opt)
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

static int config_axis (struct config_axis *a, const char *name, const char *value)
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
    else if (!strcmp (name, "initv"))
        a->initv = strtoul (value, NULL, 10);
    else if (!strcmp (name, "finalv"))
        a->finalv = strtoul (value, NULL, 10);
    else if (!strcmp (name, "guide"))
        a->guide = strtod (value, NULL);
    else if (!strcmp (name, "slow"))
        a->slow = strtod (value, NULL);
    else if (!strcmp (name, "medium"))
        a->medium = strtod (value, NULL);
    else if (!strcmp (name, "fast"))
        a->fast = strtod (value, NULL);
    else if (!strcmp (name, "sidereal"))
        a->sidereal = strtod (value, NULL);
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
    return rc;
}

static int config_cb (void *user, const char *section, const char *name,
                      const char *value)
{
    struct config *opt = user;
    int rc = 1; /* 0 = error */

    if (!strcmp (section, "t_axis"))
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
    } else if (!strcmp (section, "guide")) {
        if (!strcmp (name, "gpio")) {
            if (opt->guide_gpio)
                free (opt->guide_gpio);
            opt->guide_gpio = xstrdup (value);
        } else if (!strcmp (name, "debounce"))
            opt->guide_debounce = strtod (value, NULL);
    }
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
