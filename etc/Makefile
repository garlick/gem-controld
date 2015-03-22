include ../Makefile.inc

all:

clean:

install: config.ini gem.service
	cp config.ini $(prefix)/etc/gem.config
	cp gem.service /etc/systemd/system/gem.service
	systemctl enable gem.service
