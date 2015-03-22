all:

clean:

install: config.ini gem.service
	cp config.ini /usr/local/etc/gem.config
	cp gem.service /etc/systemd/system/gem.service
	systemctl enable gem.service
