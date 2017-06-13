
SUBDIRS = base memcache

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = mmd

lib_LTLIBRARIES = libmm.la

core_sources = \
	common.h

net_sources = \
	net/net.c net/net.h \
	net/netbuf.c net/netbuf.h

libmm_la_SOURCES = $(core_sources) $(net_sources)
libmm_la_LIBADD = base/libmmbase.la

mmd_SOURCES = main.c
mmd_LDADD = memcache/libmemcache.a libmm.la
