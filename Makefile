include Makefile.inc

SUBDIRS = libini src etc # dts

all:

all clean install install_cli:
	for dir in $(SUBDIRS); do make -C $$dir $@; done
