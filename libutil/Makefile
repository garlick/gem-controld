include ../Makefile.inc

CFLAGS = $(ZMQ_CFLAGS)

OBJS = log.o xzmalloc.o gpio.o ev_zmq.o

all: libutil.a

libutil.a: $(OBJS)
	$(AR) rv $@ $^

install_cli install:

clean:
	rm -f *.a *.o
