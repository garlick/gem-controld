include Makefile.inc

SUBDIRS = libutil libini src etc dts

all:

all clean install:
	for dir in $(SUBDIRS); do make -C $$dir $@; done
