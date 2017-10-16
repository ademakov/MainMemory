
noinst_LTLIBRARIES = libmainbase.la

AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra -DMSPACES -DUSE_DL_PREFIX

libmainbase_la_SOURCES = \
	args.c args.h \
	atomic.h \
	bitops.h \
	bitset.c bitset.h \
	bytes.h \
	cksum.c cksum.h \
	clock.c clock.h \
	combiner.c combiner.h \
	conf.c conf.h \
	cstack.c cstack.h \
	daemon.c daemon.h \
	exit.c exit.h \
	format.c format.h \
	hash.c hash.h \
	hashmap.c hashmap.h \
	json.c json.h \
	list.h \
	lock.c lock.h \
	logger.c logger.h \
	report.c report.h \
	ring.c ring.h \
	runtime.c runtime.h \
	scan.c scan.h \
	settings.c settings.h \
	stdcall.h \
	syscall.h \
	timeq.c timeq.h \
	topology.c topology.h \
	arch/intrinsic.h \
	event/backend.c event/backend.h \
	event/dispatch.c event/dispatch.h \
	event/epoch.c event/epoch.h \
	event/epoll.c event/epoll.h \
	event/event.c event/event.h \
	event/forward.c event/forward.h \
	event/kqueue.c event/kqueue.h \
	event/listener.c event/listener.h \
	event/nonblock.c event/nonblock.h \
	event/selfpipe.c event/selfpipe.h \
	fiber/async.c fiber/async.h \
	fiber/combiner.c fiber/combiner.h \
	fiber/fiber.c fiber/fiber.h \
	fiber/future.c fiber/future.h \
	fiber/runq.c fiber/runq.h \
	fiber/strand.c fiber/strand.h \
	fiber/timer.c fiber/timer.h \
	fiber/wait.c fiber/wait.h \
	fiber/work.c fiber/work.h \
	memory/alloc.h \
	memory/arena.h \
	memory/buffer.c memory/buffer.h \
	memory/chunk.c memory/chunk.h \
	memory/global.c memory/global.h \
	memory/malloc.c memory/malloc.h \
	memory/memory.c memory/memory.h \
	memory/pool.c memory/pool.h \
	memory/region.c memory/region.h \
	memory/space.c memory/space.h \
	thread/backoff.c thread/backoff.h \
	thread/barrier.c thread/barrier.h \
	thread/domain.c thread/domain.h \
	thread/ident.c thread/ident.h \
	thread/local.c thread/local.h \
	thread/monitor.c thread/monitor.h \
	thread/thread.c thread/thread.h \
	util/hook.c util/hook.h \
	util/libcall.c util/libcall.h

if ARCH_X86
libmainbase_la_SOURCES += \
	arch/x86/asm.h \
	arch/x86/atomic.h \
	arch/x86/basic.h \
	arch/x86/cstack.c arch/x86/cstack-switch.S \
	arch/x86/intrinsic.h \
	arch/x86/syscall.h arch/x86/syscall.c
endif

if ARCH_X86_64
libmainbase_la_SOURCES += \
	arch/x86-64/asm.h \
	arch/x86-64/atomic.h \
	arch/x86-64/basic.h \
	arch/x86-64/cstack.c arch/x86-64/cstack-switch.S \
	arch/x86-64/intrinsic.h \
	arch/x86-64/syscall.h arch/x86-64/syscall.c
endif

if ARCH_GENERIC
libmainbase_la_SOURCES += \
	arch/generic/atomic.h \
	arch/generic/basic.h \
	arch/generic/cstack.c \
	arch/generic/intrinsic.h \
	arch/generic/syscall.h
endif
