
SUBDIRS = base memcache

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = mmd

lib_LTLIBRARIES = libmm.la

core_sources = \
	common.h \
	core/async.c core/async.h \
	core/combiner.c core/combiner.h \
	core/core.c core/core.h \
	core/future.c core/future.h \
	core/runq.c core/runq.h \
	core/task.c core/task.h \
	core/timer.c core/timer.h \
	core/wait.c core/wait.h \
	core/work.c core/work.h

net_sources = \
	net/net.c net/net.h \
	net/netbuf.c net/netbuf.h

libmm_la_SOURCES = $(core_sources) $(net_sources)
libmm_la_LIBADD = base/libmmbase.la

mmd_SOURCES = main.c
mmd_LDADD = memcache/libmemcache.a libmm.la
