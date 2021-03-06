/* Pointing model
 *
 * Conversion from externally provided catalog mean positions to "apparent
 * place" is currently lacking, thus parallax, light deflection, annual
 * aberration, precession, and nutation are not accounted for.  We just
 * go with the catalog mean for now.
 *
 * The apparent local sidereal time (LST) is obtained (from libnova) by
 * starting with the UNIX system time (GMT), converting to Julian date,
 * converting that to apparent sidereal time, then adding the east longitude.
 * Since HA = RA - LST, the LST enables us to convert catalog RA to HA.
 *
 * A "sync" operation (one star alignment) sets a zero point correction
 * for each axis that is used to convert HA,DEC to an instrument position
 * suitable for feeding to the motion controllers.  There is currently no
 * provision for correcting for collimation errors, non-perpendicularity
 * of the axes, polar misalignment, or tube flexure.
 *
 * Refs:
 * "Telecope Pointing" by Patrick Wallce, http://www.tpointsw.uk/pointing.htm
 */

struct point;

enum {
    POINT_DEBUG = 1,
    POINT_WEST = 2,     // set initial point to western horizon, not eastern
};

struct point *point_new (void);
void point_destroy (struct point *p);

void point_set_flags (struct point *p, int flags);

/* Set observer's position (lat,lng)
 * Sign is set separately with _neg(), to support a quirk of LX200 protocol
 */
void point_set_latitude (struct point *p, int deg, int min, double sec);
void point_set_longitude (struct point *p, int deg, int min, double sec);
void point_set_longitude_neg (struct point *p, unsigned short neg);

/* Get observer's position (lat,lng)
 */
void point_get_latitude (struct point *p, int *deg, int *min, double *sec);
void point_get_longitude (struct point *p, int *deg, int *min, double *sec);

/* Set target object coordinates in (ra,dec).
 * The target object is a "register" used for syncing zero point corrections
 * and goto operations.
 */
void point_set_target_dec (struct point *p, int deg, int min, double sec);
void point_set_target_ra (struct point *p, int hr, int min, double sec);

/* Get target object coordinates in uncorrected telescope position (degrees).
 * This will be used for goto.
 */
void point_get_target (struct point *p, double *t, double *d);

/* Set internal zero point corrections so that uncorrected telescope position
 * plus zpc equals (ha,dec) of target object.
 */
void point_sync_target (struct point *p);

/* Set/update uncorrected telescope position (in degrees).
 */
void point_set_position_ha (struct point *p, double t);
void point_set_position_dec (struct point *p, double dec);

/* Get corrected telescope position in (ra,dec).
 * This is computed from uncorrected telescope position, zero point
 * corrections, and apparent local sidereal time.
 */
void point_get_position_ra (struct point *p, int *hr, int *min, double *sec);
void point_get_position_dec (struct point *p, int *deg, int *min, double *sec);

void point_get_gmtoff (struct point *p, double *offset);
void point_get_localtime (struct point *p, int *hour, int *min, double *sec);
void point_get_localdate (struct point *p, int *day, int *month, int *year);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
