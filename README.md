### gem-controld

This project is a control system for a german equatorial telescope mount.
The mount is a Parallax series 125 head that has been retrofitted
with stepper motors on RA and DEC axes.

Electronics are based on the Beaglebone Black ARM-based single board
computer running Linux, a custom interface "cape" with two
[RS-422 transceivers](http://www.ti.com/product/sn65hvd379) which connect to
[Schneider Electric IM483i](http://motion.schneider-electric.com/downloads/manuals/im483i_ie.pdf) motion controllers, and GPIO based
[autogider](http://www.store.shoestringastronomy.com/guide_port_cables.pdf) and
[handbox](http://www.bbastrodesigns.com/handpad-assembly_notes.html) interfaces.
The electronics are packaged in a 7x7x5 DIN rail enclosure that mounts to
the telescope pier.

Software is based on reactive (event-driven) programming, with the goal
of keeping complexity down and separation of concerns among modules up.

### status

The handbox can be used to slew the telescope in two speeds, and the RA axis
tracks at a fixed sidereal rate.

A Tangent BBOX style network interface allows the telescope to be synchronized
to the Sky Safari iPad app, as though it were "push-to" scope with encoders.

Autoguiding on an ST-4 interface works.

### future plans

Implement a goto interface that works with Sky Safari and other applications,
such as the LX200 protocol.
