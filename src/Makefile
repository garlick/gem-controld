include ../Makefile.inc

PROG = gem-motiond

CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir) \
	 -DCONFIG_FILENAME=\"$(prefix)/etc/gem.config\"

LIBS  = -L$(abs_topdir)/libutil -lutil \
	-L$(abs_topdir)/libini -lini \
	-lpthread -lev -lm -lrt

OBJS = configfile.o xzmalloc.o log.o gpio.o hpad.o guide.o motion.o daemon.o

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

install: $(PROG)
	cp $(PROG) $(prefix)/sbin/$(PROG)

install_cli:

clean:
	rm -f *.o $(PROG)

test:
	echo $(CFLAGS)
	echo $(LIBS)
