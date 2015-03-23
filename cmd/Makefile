include ../Makefile.inc

CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir)
LIBS =  -L$(abs_topdir)/libcommon -lcommon \
	-L$(abs_topdir)/libini -lini \
	-L$(abs_topdir)/libutil -lutil \
	-lczmq -lzmq -lpthread -lev -lm -lrt

all: gem

gem: gem.o
	$(CC) -o $@ $^ $(LIBS)

install: install_cli

install_cli: gem
	cp gem $(prefix)/bin/gem

clean:
	rm -f *.o gem
