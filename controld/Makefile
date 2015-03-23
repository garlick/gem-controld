include ../Makefile.inc

CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir)
LIBS = 	-L$(abs_topdir)/libutil -lutil \
	-L$(abs_topdir)/libcommon -lcommon \
	-L$(abs_topdir)/libini -lini \
	-lczmq -lzmq -lpthread -lev -lm -lrt 

all: gem-controld

gem-controld: hpad.o motion.o daemon.o
	$(CC) -o $@ $^ $(LIBS)

install: gem-controld
	cp gem-controld $(prefix)/sbin/gem-controld

install_cli:

clean:
	rm -f *.o gem-controld
