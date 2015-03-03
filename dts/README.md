### Beaglebone Black Device Tree Overlay

`GEM-IO` is the device tree overlay for the "cape" used in this project.

The cape includes:
* UART1 and UART2 wired to SN65HV0379 3V3 RS422 conversion chips,
in turn are wired to hearders that connect to IM483I motion controllers.
* Four GPIO lines wired to a Bartels handpad with external resistors as
follows:
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
* Four GPIO pins looped back int four other GPIO pins, to allow
`lin-guider` to use its GPIO actuator to send us autoguider pulses.
* EQEP0 wired to an indexed encoder on the RA axis for PEC.

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
