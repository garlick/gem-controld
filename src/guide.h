#include <ev.h>

enum {
    GUIDE_NONE = 0,
    GUIDE_RA_MINUS = 1,
    GUIDE_DEC_MINUS = 2,
    GUIDE_DEC_PLUS = 4,
    GUIDE_RA_PLUS = 8,
};

enum {
    GUIDE_DEBUG = 1,
};

struct guide;
typedef void (*guide_cb_t)(struct guide *g, void *arg);

struct guide *guide_new (void);
void guide_destroy (struct guide *g);

int guide_init (struct guide *g, const char *pins, double debounce,
               guide_cb_t cb, void *arg, int flags);

int guide_read (struct guide *g);

void guide_start (struct ev_loop *loop, struct guide *g);
void guide_stop (struct ev_loop *loop, struct guide *g);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
