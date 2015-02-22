include ../Makefile.inc

OBJS = ini.o

all: libini.a

libini.a: $(OBJS)
	$(AR) rv $@ $^

clean:
	rm -f *.a *.o
