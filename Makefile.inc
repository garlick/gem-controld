topdir := $(dir $(lastword $(MAKEFILE_LIST)))
abs_topdir := $(abspath $(topdir))

prefix = /usr/local
