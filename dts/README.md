### Beaglebone Black Device Tree Overlay

`GEM-IO` is the device tree overlay for the "cape" used in this project.
This description assumes debian wheezy with 3.8.13 or newer kernel.

The cape includes:
* UART1, UART2, and UART4 wired to [Texas Instrument SN65HVD379](http://www.ti.com/product/sn65hvd379) 3V3 RS422 conversion chips, in turn wired to headers that connect to [Schneider Electric IM483I](http://motion.schneider-electric.com/products/im483i_ie.html) motion controllers, which drive Oriental Motor PK245M-02BA steppers on the RA and DEC axes, and a RoboFocus geared focus motor.
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
