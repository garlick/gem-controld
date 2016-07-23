topdir := $(dir $(lastword $(MAKEFILE_LIST)))
abs_topdir := $(abspath $(topdir))

prefix = /usr/local

ZMQ_LIBS = $(shell pkg-config --libs libczmq) \
	   $(shell pkg-config --libs libzmq)
ZMQ_CFLAGS = $(shell pkg-config  --cflags libczmq) \
	     $(shell pkg-config  --cflags libzmq)
