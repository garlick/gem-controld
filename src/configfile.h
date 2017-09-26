#include <stdbool.h>

struct config_axis {
    char *device;
    int mode;
    int resolution;
    int ihold;
    int irun;
    int initv;
    int finalv;
    int accel;
    int decel;
    int steps;
    double guide;
    double slow;
    double medium;
    double fast;
    double sidereal;
};

struct config {
    struct config_axis t;
    struct config_axis d;
    bool no_motion;
    bool soft_init;
    char *hpad_gpio;
    double hpad_debounce;
    char *guide_gpio;
    double guide_debounce;
} opt_t;

void configfile_init (const char *filename, struct config *opt);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
