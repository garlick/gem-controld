include ../Makefile.inc

OBJS = ini.o

all: libini.a

libini.a: $(OBJS)
	$(AR) rv $@ $^

install_cli install:

clean:
	rm -f *.a *.o
