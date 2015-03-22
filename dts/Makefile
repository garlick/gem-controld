include ../Makefile.inc

BIN = GEM-IO-00A0.dtbo
FWDIR = /lib/firmware

VPATH = /boot/dtbs/$(shell uname -r)
SYSDTS = am335x-boneblack.dts

SLOTS = /sys/devices/bone_capemgr.?/slots
PINS = /sys/kernel/debug/pinctrl/44e10800.pinmux/pins

# Robert C Nelson's version for BBB
#DTC = /usr/local/bin/dtc
# device-tree-compiler 1.4.0+dfsg-1bborg~bpo70+20141203+1 deb works now
DTC = /usr/bin/dtc

all: $(BIN)

clean:
	rm -f $(BIN) $(SYSDTS)

install: $(BIN)
	cp $(BIN) $(FWDIR)

slots:
	cat $(SLOTS)
pins:
	cat $(PINS) | egrep -e '(890)|(894)|(89c)|(898)'

# decompile system device-tree for reference
sysdts: $(SYSDTS)

%.dtbo : %.dts
	$(DTC) -O dtb -o $@ -b -O -@ $<
%.dts : %.dtb
	$(DTC) -O dts -o $@ -I dtb $<

