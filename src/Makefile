include ../Makefile.inc

PROGS = gem-motiond test-hpad

CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir) \
	 -DCONFIG_FILENAME=\"$(prefix)/etc/gem.config\"

LIBS  = -L$(abs_topdir)/libini -lini \
	-lpthread -lev -lm -lrt

OBJS = configfile.o xzmalloc.o log.o gpio.o hpad.o guide.o motion.o

all: $(PROGS)

gem-motiond: daemon.o $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

test-hpad: test-hpad.o $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

install: $(PROGS)
	cp gem-motiond $(prefix)/sbin/gem-motiond

clean:
	rm -f *.o $(PROGS)

test:
	echo $(CFLAGS)
	echo $(LIBS)
