include ../Makefile.inc

PROGS = gem-controld test-hpad test-bbox test-lx200

CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir) \
	 -DCONFIG_FILENAME=\"$(prefix)/etc/gem.config\"

LIBS  = -L$(abs_topdir)/libini -lini \
	-lpthread -lev -lm -lrt -lnova

OBJS = configfile.o xzmalloc.o log.o gpio.o hpad.o guide.o motion.o \
	bbox.o lx200.o point.o

all: $(PROGS)

gem-controld: daemon.o $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

test-hpad: test-hpad.o $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

test-bbox: test-bbox.o $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

test-lx200: test-lx200.o $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

install: gem-controld
	cp $< $(prefix)/sbin/

clean:
	rm -f *.o $(PROGS)

test:
	echo $(CFLAGS)
	echo $(LIBS)
