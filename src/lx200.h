#include <ev.h>
#include <stdarg.h>

#define DEFAULT_LX200_PORT  4031

enum {
    LX200_DEBUG = 1,
};

/* mask returned from lx200_get_slew
 */
enum {
    LX200_SLEW_NONE = 0,
    LX200_SLEW_NORTH = 1,
    LX200_SLEW_SOUTH = 2,
    LX200_SLEW_EAST = 4,
    LX200_SLEW_WEST = 8,
};

struct lx200;
typedef void (*lx200_cb_t)(struct lx200 *lx, void *arg);

struct lx200 *lx200_new (void);
void lx200_destroy (struct lx200 *lx);

int lx200_init (struct lx200 *lx, int port, int flags);

/* Register callback that is triggered when the protocol needs
 * a position update.  Callback should call lx200_set_position().
 */
void lx200_set_position_cb  (struct lx200 *lx, lx200_cb_t cb, void *arg);

/* Register callback that is triggered when slew (virtual) buttons
 * are pressed or released.  Callback should call lx200_get_slew().
 */
void lx200_set_slew_cb  (struct lx200 *lx, lx200_cb_t cb, void *arg);

void lx200_set_position (struct lx200 *lx, int x, int y);
void lx200_set_resolution (struct lx200 *lx, int x, int y);
int lx200_get_slew (struct lx200 *lx);

void lx200_start (struct ev_loop *loop, struct lx200 *lx);
void lx200_stop (struct ev_loop *loop, struct lx200 *lx);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
