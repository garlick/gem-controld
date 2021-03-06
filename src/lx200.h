#include <ev.h>
#include <stdarg.h>

#define DEFAULT_LX200_PORT  4031

enum {
    LX200_DEBUG = 1,
    LX200_POINT_WEST = 2,
};

struct lx200;
typedef void (*lx200_cb_f)(struct lx200 *lx, void *arg);

struct lx200 *lx200_new (void);
void lx200_destroy (struct lx200 *lx);

int lx200_init (struct lx200 *lx, int port, int flags);

/* Register callback that is triggered when the protocol needs
 * a position update.  Callback should call lx200_set_position().
 */
void lx200_set_position_ha_cb  (struct lx200 *lx, lx200_cb_f cb, void *arg);
void lx200_set_position_dec_cb  (struct lx200 *lx, lx200_cb_f cb, void *arg);

/* Register callback that is triggered when slew (virtual) buttons
 * are pressed or released.  Callback should call lx200_get_slew()
 * and then make appropriate movement.
 */
void lx200_set_slew_cb  (struct lx200 *lx, lx200_cb_f cb, void *arg);

/* Register callback that is triggered when protocol wants to goto
 * the target object.  Callback should call lx200_get_target ()
 * and then move to those coordinates.
 */
void lx200_set_goto_cb  (struct lx200 *lx, lx200_cb_f cb, void *arg);

/* Register callback that is triggered when protocol wants to stop all motion.
 */
void lx200_set_stop_cb  (struct lx200 *lx, lx200_cb_f cb, void *arg);

/* Register callback that is triggered when protocol wants to know
 * tracking rate.  Callback should call lx200_set_tracking ().
 */
void lx200_set_tracking_cb  (struct lx200 *lx, lx200_cb_f cb, void *arg);

/* Update tracking rate in degress per second.
 */
void lx200_set_tracking_rate (struct lx200 *lx, double dps);

/* Set t,d position in degrees.
 */
void lx200_set_position_ha (struct lx200 *lx, double t);
void lx200_set_position_dec (struct lx200 *lx, double d);

int lx200_get_slew_direction (struct lx200 *lx);
int lx200_get_slew_rate  (struct lx200 *lx);

void lx200_get_target (struct lx200 *lx, double *t, double *d);

void lx200_start (struct ev_loop *loop, struct lx200 *lx);
void lx200_stop (struct ev_loop *loop, struct lx200 *lx);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
