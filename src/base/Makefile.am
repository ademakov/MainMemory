
noinst_LTLIBRARIES = libmmbase.la

AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra -DMSPACES -DUSE_DL_PREFIX

libmmbase_la_SOURCES = \
	args.c args.h \
	base.c base.h \
	bitops.h \
	bitset.c bitset.h \
	bytes.h \
	cksum.c cksum.h \
	clock.c clock.h \
	conf.c conf.h \
	combiner.c combiner.h \
	daemon.c daemon.h \
	exit.c exit.h \
	format.c format.h \
	hash.c hash.h \
	hashmap.c hashmap.h \
	init.c init.h \
	json.c json.h \
	list.h \
	lock.c lock.h \
	report.c report.h \
	ring.c ring.h \
	scan.c scan.h \
	settings.c settings.h \
	stdcall.h \
	timeq.c timeq.h \
	topology.c topology.h \
	event/backend.c event/backend.h \
	event/batch.c event/batch.h \
	event/dispatch.c event/dispatch.h \
	event/epoll.c event/epoll.h \
	event/event.c event/event.h \
	event/kqueue.c event/kqueue.h \
	event/listener.c event/listener.h \
	event/nonblock.c event/nonblock.h \
	event/receiver.c event/receiver.h \
	event/selfpipe.c event/selfpipe.h \
	log/debug.c log/debug.h \
	log/log.c log/log.h \
	log/trace.c log/trace.h \
	memory/alloc.h \
	memory/arena.h \
	memory/buffer.c memory/buffer.h \
	memory/chunk.c memory/chunk.h \
	memory/cstack.c memory/cstack.h \
	memory/global.c memory/global.h \
	memory/malloc.c memory/malloc.h \
	memory/memory.c memory/memory.h \
	memory/pool.c memory/pool.h \
	memory/region.c memory/region.h \
	memory/space.c memory/space.h \
	thread/backoff.c thread/backoff.h \
	thread/barrier.c thread/barrier.h \
	thread/domain.c thread/domain.h \
	thread/local.c thread/local.h \
	thread/monitor.c thread/monitor.h \
	thread/request.c thread/request.h \
	thread/thread.c thread/thread.h \
	util/hook.c util/hook.h \
	util/libcall.c util/libcall.h
