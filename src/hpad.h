#include <ev.h>

enum {
    HPAD_CONTROL_M1 = 1,
    HPAD_CONTROL_M2 = 2,
};

enum {
    HPAD_DEBUG = 1,
};

struct hpad;

typedef void (*hpad_cb_t)(struct hpad *h, void *arg);

struct hpad *hpad_new (void);
void hpad_destroy (struct hpad *h);

int hpad_init (struct hpad *h, const char *pins, double debounce,
               hpad_cb_t cb, void *arg, int flags);

int hpad_get_slew_direction (struct hpad *h);
int hpad_get_slew_rate (struct hpad *h);
int hpad_get_control (struct hpad *h);

void hpad_start (struct ev_loop *loop, struct hpad *h);
void hpad_stop (struct ev_loop *loop, struct hpad *h);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
