#include <ev.h>

enum {
    GUIDE_DEBUG = 1,
};

struct guide;
typedef void (*guide_cb_t)(struct guide *g, void *arg);

struct guide *guide_new (void);
void guide_destroy (struct guide *g);

int guide_init (struct guide *g, const char *pins, double debounce,
               guide_cb_t cb, void *arg, int flags);

int guide_get_slew_direction (struct guide *g);

void guide_start (struct ev_loop *loop, struct guide *g);
void guide_stop (struct ev_loop *loop, struct guide *g);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
