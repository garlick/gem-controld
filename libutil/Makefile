include ../Makefile.inc

OBJS = log.o xzmalloc.o gpio.o ev_zmq.o

all: libutil.a

libutil.a: $(OBJS)
	$(AR) rv $@ $^

install:

clean:
	rm -f *.a *.o
