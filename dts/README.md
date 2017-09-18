### Beaglebone Black Cape

The cape includes:
* UART1 and UART2 wired to [Texas Instrument SN65HVD379](http://www.ti.com/product/sn65hvd379) 3V3 RS422 conversion chips, in turn wired to headers that connect to [Schneider Electric IM483I](http://motion.schneider-electric.com/products/im483i_ie.html) motion controllers, which drive Oriental Motor PK245M-02BA steppers on the RA and DEC axes, and a RoboFocus geared focus motor.
* Four GPIO lines wired to a [Bartels Handpad](http://www.bbastrodesigns.com/handpad-assembly_notes.html) with a 220 ohm resistor in series.
* Four GPIO lines wired to a [ST-4 compatible guide port](http://www.store.shoestringastronomy.com/guide_port_cables.pdf) with and 220 ohm resistors in series.  Pin 1 of the guide port is no connection, while pin 2 is at 3V3.

### Device Tree overlay:

`GEM-IO` is the device tree overlay for the "cape" used in this project.
This description assumes debian wheezy with kernel 4.x or similar.

We need the device tree compiler `dtc`:
```
sudo apt-get install device-tree-compiler
```

After installing compiled dtbo file into `/lib/firmware`, add
it to `/etc/default/capemgr`.
```
CAPE=GEM-IO
```

By default GEM-IO conflicts with  `BB-BONELT-HDMI`.  Disable it by following
instructions in `/boot/uEnv.txt`.  Currently it is to uncomment this
line:
```
##BeagleBone Black: HDMI (Audio/Video)/eMMC disabled:
#dtb=am335x-boneblack-overlay.dtb
```
