
SUBDIRS = base memcache

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = maind

noinst_HEADERS = common.h
nodist_noinst_HEADERS = config.h

maind_SOURCES = main.c
maind_LDADD = memcache/libmaincache.a base/libmainbase.la
