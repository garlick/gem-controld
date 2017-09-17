#include <ev.h>

enum {
    HPAD_KEY_NONE = 0,
    HPAD_KEY_NORTH = 1,
    HPAD_KEY_SOUTH = 2,
    HPAD_KEY_WEST = 3,
    HPAD_KEY_EAST = 4,
    HPAD_KEY_M1 = 5,
    HPAD_KEY_M2 = 6,
    HPAD_MASK_KEYS = 7,
    HPAD_MASK_FAST = 8
};

struct hpad;

typedef void (*hpad_cb_t)(struct hpad *h, void *arg);

struct hpad *hpad_new (void);
void hpad_destroy (struct hpad *h);

int hpad_init (struct hpad *h, const char *pins, double debounce,
               hpad_cb_t cb, void *arg);

int hpad_read (struct hpad *h);

void hpad_start (struct ev_loop *loop, struct hpad *h);
void hpad_stop (struct ev_loop *loop, struct hpad *h);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
