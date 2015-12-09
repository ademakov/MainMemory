
SUBDIRS = arch base memcache

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = mmem

core_sources = \
	core/async.c core/async.h \
	core/combiner.c core/combiner.h \
	core/core.c core/core.h \
	core/future.c core/future.h \
	core/port.c core/port.h \
	core/runq.c core/runq.h \
	core/task.c core/task.h \
	core/timer.c core/timer.h \
	core/value.h \
	core/wait.c core/wait.h \
	core/work.c core/work.h

net_sources = \
	net/net.c net/net.h \
	net/netbuf.c net/netbuf.h

mmem_SOURCES = \
	$(core_sources) $(net_sources) \
	common.h main.c

mmem_LDADD = memcache/libmemcache.a base/libmmbase.a arch/libmmarch.a
