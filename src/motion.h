#include <stdint.h>
#include <stdbool.h>

enum {
    MOTION_DEBUG = 0x01,    /* send telemetry to stderr */
};

/* Bits for motion_set_io(), motion_get_io() mask.
 */
enum {
    MOTION_IO_INPUT1    = 0x01,
    MOTION_IO_INPUT2    = 0x02,
    MOTION_IO_INPUT3    = 0x04,
    MOTION_IO_OUTPUT1   = 0x08, // ^green_led on daughter board
    MOTION_IO_OUTPUT2   = 0x10, // ^white_led
    MOTION_IO_OUTPUT3   = 0x20, // ^blue_led
};

/* Bits for motion_get_status() mask.
 */
enum {
    MOTION_STATUS_MOVING    = 0x01, // axis moving
    MOTION_STATUS_CONSTANT  = 0x02, // constant velocity
    MOTION_STATUS_HOMING    = 0x08, // homing routine is active
    MOTION_STATUS_HUNTING   = 0x10, // encoder correction
    MOTION_STATUS_RAMPING   = 0x20, // ramping up or down
};

struct motion_config {
    int resolution;     // microstep resolution (0:8)
    int ihold;          // hold current in pct of max (0-100)
    int irun;           // hold current in pct of max (0-100)
    int accel;          // acceleration slope (0-255)
    int decel;          // deceleration slope (0-255)
    int mode;           // resolution mode (0=fixed, 1=auto)
    int initv;          // initial velocity for ramp up (20:20000)
                        //   in full steps/s (auto), or pulses/s (fixed)
    int finalv;         // final velocity for ramp up (20:20000)
                        //   in full steps/s (auto), or pulses/s (fixed)
    int steps;          // steps per 360 degrees (including gear reduction)
    bool ccw;           // true if positive motion is counter-clockwise
};

struct motion;
typedef void (*motion_cb_f)(struct motion *m, void *arg);


/* Move at fixed velocity (in steps per second), with ramp up or ramp down.
 */
int motion_move_constant (struct motion *m, int sps);

/* Move at fixed velocity (in degrees per second), with ramp up or ramp down.
 * This is a wrapper for motion_move_constant() that uses configuration
 * for steps, mode, and resolution to convert angular velocity to linear.
 */
int motion_move_constant_dps (struct motion *m, double dps);

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

/* Abort motion without deceleration.
 */
int motion_abort (struct motion *m);

/* Read moving status.
 */
int motion_get_status (struct motion *m, int *status);

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

/* Set callback to be called when a goto has completed.
 */
void motion_set_cb (struct motion *m, motion_cb_f cb, void *arg);

/* Initialization
 * Performs a reset, equivalent to the power-up condition (zeroes origin).
 * Configure from motion_config struct, or if NULL, use nvram settings.
 */
int motion_init (struct motion *m, const char *device,
                 struct motion_config *cfg, int flags);

struct motion *motion_new (const char *name);
void motion_destroy (struct motion *m);

/* Start/stop watchers.
 */
void motion_start (struct ev_loop *loop, struct motion *m);
void motion_stop (struct ev_loop *loop, struct motion *m);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
