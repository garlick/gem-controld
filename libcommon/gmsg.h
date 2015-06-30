#include <stdint.h>
#include <stdio.h>

typedef struct gmsg_struct *gmsg_t;

gmsg_t gmsg_create (uint8_t op);
void gmsg_destroy (gmsg_t *g);

int gmsg_set_op (gmsg_t g, uint8_t op);
int gmsg_get_op (gmsg_t g, uint8_t *op);

int gmsg_set_flags (gmsg_t g, uint32_t flags);
int gmsg_get_flags (gmsg_t g, uint32_t *flags);

int gmsg_set_arg1 (gmsg_t g, int32_t arg);
int gmsg_get_arg1 (gmsg_t g, int32_t *arg);

int gmsg_set_arg2 (gmsg_t g, int32_t arg);
int gmsg_get_arg2 (gmsg_t g, int32_t *arg);

int gmsg_set_error (gmsg_t g, int32_t errnum);
int gmsg_error (gmsg_t g);

void gmsg_dump (FILE *f, gmsg_t g, const char *prefix);

gmsg_t gmsg_recv (void *sock);
int gmsg_send (void *sock, gmsg_t g);

/* Operations
 * Coordinate units are hundredth-arcsec
 * Velocity units are hundredth-arcsec/sec
 */
enum {
    OP_ORIGIN = 0,      /* set origin to current position */
    OP_PARK = 1,        /* park the telescope and stop tracking */
    OP_STOP = 2,        /* stop tracking */
    OP_TRACK = 3,       /* start tracking (optional args) */
    OP_GOTO = 4,        /* slew to specified coordinates */
    OP_POSITION = 5,    /* get current position */
};

/* Flags
 */
enum {
    FLAG_ERROR      = 0x0001,       /* request failed (see arg1 for errnum) */
    FLAG_ARG1       = 0x0002,       /* arg1 is valid */
    FLAG_ARG2       = 0x0004,       /* arg2 is valid */

    FLAG_T_TRACKING   = 0x0100,     /* t axis moving at constant velocity */
    FLAG_T_MOVING     = 0x0200,     /* t axis goto/park in progress */

    FLAG_D_TRACKING   = 0x0400,     /* d axis moving at constant velocity */
    FLAG_D_MOVING     = 0x0800,     /* d axis goto/park in progress */
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
