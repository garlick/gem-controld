include Makefile.inc

SUBDIRS = libutil libini libcommon cmd controld etc dts

all:

all clean install install_cli:
	for dir in $(SUBDIRS); do make -C $$dir $@; done
