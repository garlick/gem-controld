#include <ev.h>
#include <stdarg.h>

#define DEFAULT_BBOX_PORT  4030

enum {
    BBOX_DEBUG = 1,
};

/* Implement Tangent bbox protocol, or something close enough...
 */

struct bbox;
typedef void (*bbox_cb_t)(struct bbox *bb, void *arg);

struct bbox *bbox_new (void);
void bbox_destroy (struct bbox *bb);

int bbox_init (struct bbox *g, int port, bbox_cb_t cb, void *arg, int flags);

void bbox_set_position (struct bbox *bb, int x, int y);
void bbox_set_resolution (struct bbox *bb, int x, int y);

void bbox_start (struct ev_loop *loop, struct bbox *bb);
void bbox_stop (struct ev_loop *loop, struct bbox *bb);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
