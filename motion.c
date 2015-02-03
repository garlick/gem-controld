#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "motion.h"

static int dgetc (int fd)
{
    uint8_t c;
    if (read (fd, &c, 1) != 1)
        return -1;
    return c;
}

static void trim (char *s)
{
    int len = strlen (s);
    while (len > 0 && isspace (s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

/* Read a \n-termiated line and return it without terminating whitespace.
 * The result will always be NULL terminated.
 * Return NULL on error, e.g. read(2) returns <= 0, or buf exhausted.
 */
static char *dgets (int fd, char *buf, int size)
{
    int len = 0;
    int c;
    do {
        if (len >= size - 2)
            return NULL; /* overflow */
        if ((c = dgetc (fd)) < 0)
            return NULL; /* read error/EOF */
        buf[len++] = c;
    } while (c != '\n');
    buf[len] = '\0';
    trim (buf);
    return buf;
}

int motion_init (const char *devname)
{
    int fd, saved_errno, flags;
    struct termios tio;
    uint8_t c;
    char buf[80];

    if ((fd = open(devname, O_RDWR | O_NOCTTY)) < 0)
        goto error;
    if (tcgetattr(fd, &tio) < 0)
        goto error;
    cfsetspeed(&tio, B9600);/* 9600,8N1*/
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    if (tcsetattr(fd, TCSANOW, &tio) < 0)
        goto error;

    if (dprintf (fd, "\003") < 0)   /* send ctrl-C to reset IM483I */
        goto error;
    usleep (1000*200);              /* (wait 200ms to come out of reset) */
    if (dprintf (fd, " ") < 0)      /* send space to initiate comms */
        goto error;
    if (dgets (fd, buf, sizeof (buf)) < 0) /* read "4038 ADVANCED...INC" */
        goto error;
    if (dgets (fd, buf, sizeof (buf)) < 0) /* read "MAX-2000 v1.15i" */
        goto error;
    if (dprintf (fd, "\r") < 0)     /* send \r to elicit a # response */
        goto error;
    if (dgets (fd, buf, sizeof (buf)) < 0) /* read "#" */
        goto error;
    if (dprintf (fd, "A8") < 0)     /* turn off green LED wired to OUTPUT 1 */
        goto error;
#if 0
    flags = fcntl (fd, F_GETFD);
    if (flags < 0)
        goto error;
    if (fcntl (fd, F_SETFD, flags | O_NONBLOCK) < 0)
        goto error;
#endif
    return fd;
error:
    saved_errno = errno;
    motion_fini (fd);
    errno = saved_errno;
    return -1;
}

void motion_fini (int fd)
{
    if (fd >= 0)
        (void)close (fd);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
