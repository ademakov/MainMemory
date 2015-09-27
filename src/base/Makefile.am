
noinst_LIBRARIES = libmmbase.a

AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra -DMSPACES -DUSE_DL_PREFIX

libmmbase_a_SOURCES = \
	backoff.c backoff.h \
	barrier.c barrier.h \
	base.c base.h \
	bitops.h \
	bitset.c bitset.h \
	bytes.h \
	cksum.c cksum.h \
	clock.c clock.h \
	combiner.c combiner.h \
	hash.c hash.h \
	list.h \
	lock.c lock.h \
	ring.c ring.h \
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
	log/error.c log/error.h \
	log/log.c log/log.h \
	log/plain.c log/plain.h \
	log/trace.c log/trace.h \
	memory/alloc.c memory/alloc.h \
	memory/arena.h memory/arena.c \
	memory/buffer.c memory/buffer.h \
	memory/chunk.c memory/chunk.h \
	memory/cstack.c memory/cstack.h \
	memory/malloc.c memory/malloc.h \
	memory/memory.c memory/memory.h \
	memory/pool.c memory/pool.h \
	memory/region.c memory/region.h \
	memory/slider.c memory/slider.h \
	memory/space.c memory/space.h \
	thread/domain.c thread/domain.h \
	thread/local.c thread/local.h \
	thread/monitor.c thread/monitor.h \
	thread/request.c thread/request.h \
	thread/thread.c thread/thread.h \
	util/exit.c util/exit.h \
	util/format.c util/format.h \
	util/hook.c util/hook.h \
	util/libcall.c util/libcall.h
