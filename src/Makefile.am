
SUBDIRS = libcoro

bin_PROGRAMS = mmem

mmem_CFLAGS = -std=c99

mmem_SOURCES = \
	common.h event.c event.h list.h main.c net.c net.h \
	pool.c pool.h sched.c sched.h task.c task.h util.c util.h \
	dlmalloc/malloc.c dlmalloc/malloc.h
