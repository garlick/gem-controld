BIN = GEM-IO-00A0.dtbo

SLOTS = /sys/devices/bone_capemgr.?/slots

all: $(BIN)

list:
	cat $(SLOTS)

load:
	bash -c "echo GEM-IO >$(SLOTS)"

clean:
	rm -f $(BIN)

%.dtbo : %.dts
	dtc -O dtb -o $@ -b -O -@ $<
	
install: $(BIN)
	cp $(BIN) /lib/firmware
