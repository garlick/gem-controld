### gem-controld

This project is firmware for a german equatorial mount for telescopes.
The mount is a Parallax series 125 head that has been retrofitted
with 400-step stepper motors on RA and DEC axes.

Electronics are based on the Beaglebone Black ARM-based single board
computer running Linux, a custom interface "cape", and off the shelf
motion controllers.  This plus an ethernet switch and powered USB hub
on DIN rail are mounted in a 7x7x5 inch enclosure attached to the
telescope pier.

#### Completed

* RS-422 interface to motion controllers using two Beaglebone serial ports
* Handpad interface using four GPIO pins
* ini-style config file
* device tree overlay
* constant velocity tracking in RA
* Handpad control of both axes, fast and slow speeds
* Event loop

#### Still working on

* encoder interface for PEC
* ST-4 style guiding input
* RA velocity tuning
* zeromq interface for remote autoguiding and status
* LX200 protocol emulation for Sky Safari control
