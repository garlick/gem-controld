### Device Tree overlay

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
