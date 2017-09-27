#include <stdint.h>

enum {
    MOTION_DEBUG = 0x01,    /* send telemetry to stderr */
    MOTION_RESET = 0x02,    /* perform factory reset */
};

enum {
    MOTION_IO_INPUT1    = 0x01,
    MOTION_IO_INPUT2    = 0x02,
    MOTION_IO_INPUT3    = 0x04,
    MOTION_IO_OUTPUT1   = 0x08, // ^green_led on daughter board
    MOTION_IO_OUTPUT2   = 0x10, // ^white_led
    MOTION_IO_OUTPUT3   = 0x20, // ^blue_led
};

enum {
    MOTION_STATUS_MOVING    = 0x01,
    MOTION_STATUS_TRACKING  = 0x02,
    MOTION_STATUS_HOMING    = 0x08,
    MOTION_STATUS_HUNTING   = 0x10,
    MOTION_STATUS_RAMPING   = 0x20,
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
int motion_move_constant (struct motion *m, int velocity);

/* Query current position.
 */
int motion_get_position (struct motion *m, double *position);

/* Slew the to absolute or relative position.  position/offset is in full
 * steps, with a resolution of 0.01 step.  Motor will ramp up and ramp down
 * automatically.
 */
int motion_goto_absolute (struct motion *m, double position);
int motion_goto_relative (struct motion *m, double offset);

/* Execute a "soft stop" (with deceleration) on all motion.
 */
int motion_soft_stop (struct motion *m);

/* Read moving status.
 */
int motion_get_status (struct motion *m, uint8_t *status);

/* Set internal position counter to zero.
 */
int motion_set_origin (struct motion *m);

/* Read/write io port
 */
int motion_get_io (struct motion *m, uint8_t *val);
int motion_set_io (struct motion *m, uint8_t val);

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
