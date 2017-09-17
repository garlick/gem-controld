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
    double slow;
    double fast;
    int offset;
    int park;
    int high_limit;
    int low_limit;
};

struct config {
    struct config_axis t;
    struct config_axis d;
    bool debug;
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
