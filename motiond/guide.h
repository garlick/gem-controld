#include <ev.h>

enum {
    GUIDE_RA_MINUS = 1,
    GUIDE_DEC_MINUS = 2,
    GUIDE_DEC_PLUS = 4,
    GUIDE_RA_PLUS = 8,
};

typedef struct guide_struct guide_t;
typedef void (*guide_cb_t)(guide_t *g, void *arg);

guide_t *guide_new (void);
void guide_destroy (guide_t *g);

int guide_init (guide_t *g, const char *pins, double debounce,
               guide_cb_t cb, void *arg);

int guide_read (guide_t *g);

void guide_start (struct ev_loop *loop, guide_t *g);
void guide_stop (struct ev_loop *loop, guide_t *g);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
