include ../Makefile.inc

all:

clean:

install: gem.service install_cfg
	cp gem.service /etc/systemd/system/gem.service
	systemctl enable gem.service

install_cfg: config.ini
	cp config.ini $(prefix)/etc/gem.config
