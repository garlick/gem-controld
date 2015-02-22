include Makefile.inc

SUBDIRS = libutil libini src

all:

all clean install:
	for dir in $(SUBDIRS); do make -C $$dir $@; done
