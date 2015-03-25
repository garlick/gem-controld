include ../Makefile.inc

PROG = gem-motiond
CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir)
LIBS = 	-L$(abs_topdir)/libutil -lutil \
	-L$(abs_topdir)/libcommon -lcommon \
	-L$(abs_topdir)/libini -lini \
	-lczmq -lzmq -lpthread -lev -lm -lrt 

all: $(PROG)

$(PROG): hpad.o motion.o daemon.o
	$(CC) -o $@ $^ $(LIBS)

install: $(PROG)
	cp $(PROG) $(prefix)/sbin/$(PROG)

install_cli:

clean:
	rm -f *.o $(PROG)
