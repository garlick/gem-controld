typedef struct gpio_struct *gpio_t;

/* 'gpio' is an array of 'len' integers representing the GPIO
 * pin assignments.  gpio[0] = bit0 gpio pin assignment,
 * gpio[1] = bit1 gpio pin assignment, etc., and pin numbers
 * correspond to the /sys/class/gpio/gpioN numbering.
 */
gpio_t gpio_init (int *gpio, int len);
void gpio_fini (gpio_t g);
int gpio_event (gpio_t g);
