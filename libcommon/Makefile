include ../Makefile.inc

CFLAGS = -I$(abs_topdir) $(ZMQ_CFLAGS) \
	-DCONFIG_FILENAME=\"$(prefix)/etc/gem.config\"
LIBS = -L$(abs_topdir)/libutil -lutil $(ZMQ_LIBS)

OBJS = configfile.o gmsg.o

all: libcommon.a

libcommon.a: $(OBJS)
	$(AR) rv $@ $^

install_cli install:

clean:
	rm -f *.a *.o
