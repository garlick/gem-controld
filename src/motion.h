#include <stdint.h>

/* These interfaces presume that config values are set with minicom
 * and stored in nvram.
 */
enum {
    MOTION_DEBUG = 0x01,
};

typedef struct motion_struct *motion_t;


/* Connect to indexer on 'devname' and conduct a little dialog.
 * 'flags' may be 0 or MOTION_DEBUG.
 */
motion_t motion_init (const char *devname, const char *name, int flags);

/* Disconnect from indexer, stopping motion if any.
 */
void motion_fini (motion_t m);

const char *motion_name (motion_t m);

/* Set microstep resolution (0:8)
 */
int motion_set_resolution (motion_t m, int resolution);

/* Set hold/run current limit in pct of max (0-100)
 */
int motion_set_current (motion_t m, int hold, int run);

/* Set acceleration/deceleration slope (0-255)
 */
int motion_set_acceleration (motion_t m, int accel, int decel);

/* Set resolution mode
 * 0=fixed, 1=auto
 */
int motion_set_mode (motion_t m, int mode);

/* Move at fixed velocity, 0, +-20:20000 (+ = CW, - = CCW)
 * with ramp up or ramp down.
 */
int motion_set_velocity (motion_t m, int velocity);

/* Query current position.
 */
int motion_get_position (motion_t m, double *position);

/* Slew to position relative to origin.
 */
int motion_set_position (motion_t m, double position);

/* Slew the to current position + offset.  Offset is in full steps, with
 * a resolution of 0.01 step.  Motor will ramp up and ramp down automatically.
 */
int motion_set_index (motion_t m, double offset);

/* Read moving status.
 * If status is nonzero, device is in motion.
 */
int motion_get_status (motion_t m, uint8_t *status);

/* Set internal position counter to zero.
 */
int motion_set_origin (motion_t m);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
