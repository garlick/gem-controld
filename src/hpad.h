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

typedef struct hpad_struct *hpad_t;
typedef void (*hpad_cb_t)(hpad_t h, void *arg);

hpad_t hpad_new (void);
void hpad_destroy (hpad_t h);

int hpad_init (hpad_t h, const char *pins, double debounce,
               hpad_cb_t cb, void *arg);

int hpad_read (hpad_t h);

void hpad_start (struct ev_loop *loop, hpad_t h);
void hpad_stop (struct ev_loop *loop, hpad_t h);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
