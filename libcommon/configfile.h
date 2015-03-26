typedef struct {
    char *device;
    int mode;
    int resolution;
    int ihold;
    int irun;
    int accel;
    int decel;
    int steps;
    double slow;
    double fast;
    int offset;
    int park;
    int high_limit;
    int low_limit;
} opt_axis_t;

typedef struct {
    opt_axis_t t;
    opt_axis_t d;
    bool debug;
    bool no_motion;
    bool soft_init;
    char *hpad_gpio;
    double hpad_debounce;
    char *req_uri;
} opt_t;

void configfile_init (const char *filename, opt_t *opt);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
