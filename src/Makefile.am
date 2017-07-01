
SUBDIRS = base memcache

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = maind

lib_LTLIBRARIES = libmain.la

core_sources = \
	common.h

net_sources = \
	net/net.c net/net.h \
	net/netbuf.c net/netbuf.h

libmain_la_SOURCES = $(core_sources) $(net_sources)
libmain_la_LIBADD = base/libmainbase.la

maind_SOURCES = main.c
maind_LDADD = memcache/libmaincache.a libmain.la
