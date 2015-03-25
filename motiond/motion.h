#include <stdint.h>

enum {
    MOTION_DEBUG = 0x01,    /* send telemetry to stderr */
    MOTION_RESET = 0x02,    /* perform factory reset */
};

typedef struct motion_struct *motion_t;

/* Initialize communications with indexer on 'devname'.
 */
motion_t motion_init (const char *devname, const char *name, int flags,
                      bool *coldstart);

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

/* Execute a "soft stop" (with deceleration) on all motion.
 */
int motion_stop (motion_t m);

/* Read moving status.
 * If status is nonzero, device is in motion.
 */
int motion_get_status (motion_t m, uint8_t *status);

/* Set internal position counter to zero.
 */
int motion_set_origin (motion_t m);

/* Read 6-bit GPIO port.  Bits are:
 *  0 in-1
 *  1 in-2
 *  2 in-3
 *  3 out-1   (green LED: 0=on, 1=off)
 *  4 out-2
 *  5 out-3
 */
#define GREEN_LED_MASK  (8)
int motion_get_port (motion_t m, uint8_t *val);

/* Write 6-bit GPIO port.
 */
int motion_set_port (motion_t m, uint8_t val);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
