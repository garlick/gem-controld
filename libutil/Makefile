include ../Makefile.inc

OBJS = log.o xzmalloc.o

all: libutil.a

libutil.a: $(OBJS)
	$(AR) rv $@ $^

clean:
	rm -f *.a *.o
