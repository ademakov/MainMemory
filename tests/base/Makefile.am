
noinst_PROGRAMS = combiner lock ring-mpmc ring-spsc

AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra

combiner_SOURCES = combiner.c params.c params.h runner.c runner.h

lock_SOURCES = lock.c params.c params.h runner.c runner.h

ring_mpmc_SOURCES = ring-mpmc.c params.c params.h runner.c runner.h

ring_spsc_SOURCES = ring-spsc.c params.c params.h runner.c runner.h

LDADD = $(top_builddir)/src/base/libmmbase.a
