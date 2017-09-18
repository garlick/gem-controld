### gem-controld

This project is a control system for a german equatorial telescope mount.
The mount is a Parallax series 125 head that has been retrofitted
with stepper motors on RA and DEC axes.

Electronics are based on the Beaglebone Black ARM-based single board
computer running Linux, a custom interface "cape", two RS-422 based
IM483i motion controllers, and GPIO based autogider and handbox interfaces.
The electronics are packaged in a 7x7x5 DIN rail enclosure that
mounts to the telescope pier.

[This page](https://github.com/garlick/gem-controld/blob/master/dts/README.md)
describes the hardware in further detail.

### status

The handbox can be used to slew the telescope in two speeds, and the RA axis
tracks at a fixed sidereal rate of 15.0417 arcsec/sec.

Partially completed: autoguiding interface.

Near term: a Tangent BBOX style interface that will allow
the telescope to be synchronized to the SkySafari iPad app, as though
it were "push-to" scope wtih encoders.

Later: goto interface to SkySafari, KStars, or TheSky.
