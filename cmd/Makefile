include ../Makefile.inc

CFLAGS += -Wall -D_GNU_SOURCE=1 -I$(abs_topdir) \
	-DPOSITION_GP=\"$(prefix)/etc/gem-position.gp\"
	$(ZMQ_CFLAGS)
LIBS +=  -L$(abs_topdir)/libcommon -lcommon \
	-L$(abs_topdir)/libini -lini \
	-L$(abs_topdir)/libutil -lutil \
	$(ZMQ_LIBS) -lpthread -lev -lm -lrt

all: gem

gem: gem.o
	$(CC) -o $@ $^ $(LIBS)

install: install_cli

install_cli: gem position.gp
	cp gem $(prefix)/bin/gem
	cp position.gp $(prefix)/etc/gem-position.gp

clean:
	rm -f *.o gem
