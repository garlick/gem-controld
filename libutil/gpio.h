#include <stdbool.h>

/* All functions return 0 on succes, or
 * -1 on error with errno set, 
 */

/* Export/unexport  a GPIO pin.
 */
int gpio_set_export (int pin, bool val);

/* Set port direction.  'direction' should be "in", "out", "low", or "high".
 * The latter two set direction to output and write an initial value.
 */
int gpio_set_direction (int pin, const char *direction);

/* Configure interrupt 'edge' to "none", "rising", "falling", or "both".
 */
int gpio_set_edge (int pin, const char *edge);

/* Configure port polarity (affects edge, read, write).
 */
int gpio_set_polarity (int pin, bool active_high);

/* Open GPIO pin, returning file descriptor.
 * The file descriptor should be closed with close () when no longer needed.
 */
int gpio_open (int pin, int mode);

/* Read GPIO pin by file descriptor.
 */
int gpio_read (int fd, int *val);

/* Write GPIO pin by file descriptor.
 */
int gpio_write (int fd, int val);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
