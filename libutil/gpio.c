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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "gpio.h"

#ifndef PATH_MAX
#define PATH_MAX    1024
#endif
#define INTBUFLEN   16

int gpio_set_export (int pin, bool val)
{
    struct stat sb;
    char path[PATH_MAX];
    char msg[INTBUFLEN];
    FILE *fp;
    int rc = -1;

    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d", pin);
    rc = stat (path, &sb);
    if ((val && rc == 0) || (!val && rc == -1)) {
        rc = 0;
        goto done;
    }
    snprintf (path, sizeof (path), "/sys/class/gpio/%s", val ? "export"
                                                             : "unexport");
    if (!(fp = fopen (path, "w")))
        goto done;
    snprintf (msg, sizeof (msg), "%d", pin);
    if (fputs (msg, fp) < 0) {
        (void)fclose (fp);
        goto done;
    }
    if (fclose (fp) != 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int gpio_set_direction (int pin, const char *direction)
{
    char path[PATH_MAX];
    FILE *fp;
    int rc = -1;

    if (strcmp (direction, "in") != 0 && strcmp (direction, "out") != 0
     && strcmp (direction, "low") != 0 && strcmp (direction, "high") != 0) {
        errno = EINVAL;
        goto done;
    }
    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d/direction", pin);
    if (!(fp = fopen (path, "w")))
        goto done;
    if (fputs (direction, fp) < 0) {
        (void)fclose (fp);
        goto done;
    }
    if (fclose (fp) != 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int gpio_set_edge (int pin, const char *edge)
{
    char path[PATH_MAX];
    FILE *fp;
    int rc = -1;

    if (strcmp (edge, "none") != 0 && strcmp (edge, "both") != 0
     && strcmp (edge, "rising") != 0 && strcmp (edge, "falling") != 0) {
        errno = EINVAL;
        goto done;
    }
    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d/edge", pin);
    if (!(fp = fopen (path, "w")))
        goto done;
    if (fputs (edge, fp) < 0) {
        (void)fclose (fp);
        goto done;
    }
    if (fclose (fp) != 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int gpio_set_polarity (int pin, bool active_high)
{
    char path[PATH_MAX];
    FILE *fp;
    int rc = -1;

    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d/active_low", pin);
    if (!(fp = fopen (path, "w")))
        goto done;
    if (fputs (active_high ? "0" : "1", fp) < 0) {
        (void)fclose (fp);
        goto done;
    }
    if (fclose (fp) != 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int gpio_read (int fd, int *val)
{
    char c;
    int n;
    int rc = -1;

    if (lseek (fd, 0, SEEK_SET) < 0)
        goto done;
    n = read (fd, &c, 1);
    if (n < 0)
        goto done;
    if (n == 0) {
        errno = EIO;
        goto done;
    } 
    *val = (c == '0' ? 0 : 1);
    rc = 0;
done:
    return rc;
}

int gpio_write (int fd, int val)
{
    char c;
    int rc = -1;

    if (lseek (fd, 0, SEEK_SET) < 0)
        goto done;
    c = val ? '1' : '0';
    if (write (fd, &c, 1) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int gpio_open (int pin, int mode)
{
    char path[PATH_MAX];
    snprintf (path, sizeof (path), "/sys/class/gpio/gpio%d/value", pin);
    return open (path, mode);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
