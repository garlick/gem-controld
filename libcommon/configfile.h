typedef struct {
    char *device;
    int mode;
    int resolution;
    int track;
    int slow;
    int fast;
    int ihold;
    int irun;
    int accel;
    int decel;
} opt_axis_t;

typedef struct {
    opt_axis_t ra;
    opt_axis_t dec;
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
