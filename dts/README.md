### Beaglebone Black Device Tree Overlay

`GEM-IO` is the device tree overlay for the "cape" used in this project.
This description assumes debian wheezy with 3.8.13 or newer kernel.

The cape includes:
* UART1 and UART2 wired to [Texas Instrument SN65HVD379](http://www.ti.com/product/sn65hvd379) 3V3 RS422 conversion chips, in turn wired to headers that connect to [Schneider Electric IM483I](http://motion.schneider-electric.com/products/im483i_ie.html) motion controllers, which drive Oriental Motor PK245M-02BA steppers on the RA and DEC axes.
* Four GPIO lines wired to a [Bartels Handpad](http://www.bbastrodesigns.com/handpad-assembly_notes.html) with external resistors as follows:
```
^ 3V3
|
|    __n__
+----o    o----/\/\/\---- GPIO
          |      220
          <
          > 10K
          <
          |
         GND
```
* Four GPIO outputs looped back into four GPIO inputs, to allow [lin_guider](http://sourceforge.net/projects/linguider) to use its GPIO actuator to send us autoguide pulses.
* EQEP0 wired to an US Digital S5S-32-IB indexed encoder on the RA axis for PEC.

There you have the whole hardware design in a nutshell!

### Installing the overlay

We need the device tree compiler `dtc`:
```
sudo apt-get install device-tree-compiler
```

After installing compiled dtbo file into `/lib/firmware`, add
it to `/etc/default/capemgr`
```
CAPE=GEM-IO
```

By default GEM-IO conflicts with  `BB-BONELT-HDMI`.  Disable it plus
some other stuff we aren't using as follows:

Mount boot partition
```
# mount /dev/mmcblk0p1 /boot/uboot
```

Add this line to `/boot/uboot/uEnv.txt`
```
cape_disable=capemgr.disable_partno=BB-BONELT-HDMI,BB-BONELT-HDMIN,BB-BONE-EMMC-2G
```
