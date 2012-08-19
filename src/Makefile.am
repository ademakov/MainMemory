
SUBDIRS = libcoro

bin_PROGRAMS = mmem

mmem_CFLAGS = -std=c99 -DENABLE_TRACE=1

mmem_SOURCES = \
	event.c event.h main.c util.c util.h \
	dlmalloc/malloc.c dlmalloc/malloc.h
