#include <stdint.h>

enum {
    MOTION_DEBUG = 0x01,    /* send telemetry to stderr */
    MOTION_RESET = 0x02,    /* perform factory reset */
};

struct motion;

/* Set microstep resolution (0:8)
 */
int motion_set_resolution (struct motion *m, int resolution);

/* Set hold/run current limit in pct of max (0-100)
 */
int motion_set_current (struct motion *m, int hold, int run);

/* Set acceleration/deceleration slope (0-255)
 */
int motion_set_acceleration (struct motion *m, int accel, int decel);

/* Set resolution mode
 * 0=fixed, 1=auto
 */
int motion_set_mode (struct motion *m, int mode);

/* Set initial velocity for ramp-up, 20:20000
 * In full steps per sec (auto mode), or pulses per sec (fixed mode).
 */
int motion_set_initial_velocity (struct motion *m, int velocity);

/* Set fnial velocity for ramp-up, 20:20000
 * In full steps per sec (auto mode), or pulses per sec (fixed mode).
 */
int motion_set_final_velocity (struct motion *m, int velocity);

/* Move at fixed velocity, 0, +-20:20000 (+ = CW, - = CCW)
 * with ramp up or ramp down.
 */
int motion_set_velocity (struct motion *m, int velocity);

/* Query current position.
 */
int motion_get_position (struct motion *m, double *position);

/* Slew to position relative to origin.
 */
int motion_set_position (struct motion *m, double position);

/* Slew the to current position + offset.  Offset is in full steps, with
 * a resolution of 0.01 step.  Motor will ramp up and ramp down automatically.
 */
int motion_set_index (struct motion *m, double offset);

/* Execute a "soft stop" (with deceleration) on all motion.
 */
int motion_stop (struct motion *m);

/* Read moving status.
 */
#define MOTION_STATUS_MOVING    0x01
#define MOTION_STATUS_TRACKING  0x02
#define MOTION_STATUS_HOMING    0x08
#define MOTION_STATUS_HUNTING   0x10
#define MOTION_STATUS_RAMPING   0x20
int motion_get_status (struct motion *m, uint8_t *status);

/* Set internal position counter to zero.
 */
int motion_set_origin (struct motion *m);

/* Read 6-bit GPIO port.  Bits are:
 *  0 in-1
 *  1 in-2
 *  2 in-3
 *  3 out-1   (green LED: 0=on, 1=off)
 *  4 out-2   (white LED: 0=on, 1=off)
 *  5 out-3   (blue LED: 0=on, 1=off)
 */
#define GREEN_LED_MASK  (1<<3)
#define WHITE_LED_MASK  (1<<4)
#define BLUE_LED_MASK  (1<<5)
int motion_get_port (struct motion *m, uint8_t *val);

/* Write 6-bit GPIO port.
 */
int motion_set_port (struct motion *m, uint8_t val);

/* Get name associated with motion axis at creation.
 */
const char *motion_get_name (struct motion *m);

/* Initialization
 */
int motion_init (struct motion *m, const char *device, int flags,
                 bool *coldstart);

struct motion *motion_new (const char *name);
void motion_destroy (struct motion *m);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
