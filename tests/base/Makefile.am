
AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra

LDADD = $(top_builddir)/src/base/libmainbase.la

noinst_PROGRAMS = combiner-bench lock-bench ring-mpmc-bench ring-spsc-bench

combiner_bench_SOURCES = combiner-bench.c params.c params.h runner.c runner.h

lock_bench_SOURCES = lock-bench.c params.c params.h runner.c runner.h

ring_mpmc_bench_SOURCES = ring-mpmc-bench.c params.c params.h runner.c runner.h

ring_spsc_bench_SOURCES = ring-spsc-bench.c params.c params.h runner.c runner.h
