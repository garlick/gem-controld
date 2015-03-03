### Beaglebone Black Device Tree Overlay

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
