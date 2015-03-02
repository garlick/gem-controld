OVERLAYS = GEM-IO-00A0.dtbo

all: $(OVERLAYS)

clean:
	rm -f $(OVERLAYS)

%.dtbo : %.dts
	dtc -O dtb -o $@ -b -O -@ $<
	
	
