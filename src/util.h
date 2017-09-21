struct util;

enum {
    UTIL_DEBUG = 1,
};

struct util *util_new (void);
void util_destroy (struct util *u);

void util_set_flags (struct util *u, int flags);

/* Set latitude/longitude.  Degress may be signed.
 * Returns 0 on success, -1 on failure (invalid input).
 */
void util_set_latitude (struct util *u, int deg, int min, double sec);
void util_set_longitude (struct util *u, int deg, int min, double sec);

/* Set target object coordinates.
 */
void util_set_target_dec (struct util *u, int deg, int min, double sec);
void util_set_target_ra (struct util *u, int hr, int min, double sec);

/* Set telescope position t,d in degrees
 */
void util_set_position (struct util *u, double t, double d);

/* Get telescope ra,dec position
 */
void util_get_position_ra (struct util *u, int *hr, int *min, double *sec);
void util_get_position_dec (struct util *u, int *deg, int *min, double *sec);

/* Set internal offsets so that telsecope position points to target object
 */
void util_sync_target (struct util *u);


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
