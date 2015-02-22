include ../Makefile.inc

CFLAGS = -Wall -D_GNU_SOURCE=1 -I$(abs_topdir)
LIBS = -lev -lm -lrt \
	-L$(abs_topdir)/libini \
	-lini -L$(abs_topdir)/libutil -lutil

OBJS = gem.o motion.o gpio.o

all: gem

gem: $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -f $(OBJS) gem
