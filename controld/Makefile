include ../Makefile.inc

CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir) \
	-DCONFIG_FILENAME=\"$(prefix)/etc/gem.config\"
LIBS = -lev -lm -lrt \
	-L$(abs_topdir)/libini \
	-lini -L$(abs_topdir)/libutil -lutil -lczmq -lzmq -lpthread

all: gem-controld

gem-controld: hpad.o motion.o gem.o
	$(CC) -o $@ $^ $(LIBS)

install: gem-controld
	cp gem-controld $(prefix)/sbin/gem-controld

install_cli:

clean:
	rm -f *.o gem-controld
