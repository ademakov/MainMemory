
lib_LTLIBRARIES = libmainbase.la
noinst_LTLIBRARIES = libcksum.la

AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra -DMSPACES -DUSE_DL_PREFIX

libcksum_la_SOURCES = \
	cksum.c cksum.h
libcksum_la_CFLAGS = @ARCH_CFLAGS@

libmainbase_la_SOURCES = \
	args.c args.h \
	async.c async.h \
	atomic.h \
	bitops.h \
	bitset.c bitset.h \
	bytes.h \
	clock.c clock.h \
	combiner.c combiner.h \
	conf.c conf.h \
	context.c context.h \
	counter.h \
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
	task.c task.h \
	timepiece.c timepiece.h \
	timeq.c timeq.h \
	topology.c topology.h \
	arch/intrinsic.h \
	event/backend.c event/backend.h \
	event/dispatch.c event/dispatch.h \
	event/epoch.c event/epoch.h \
	event/epoll.c event/epoll.h \
	event/event.c event/event.h \
	event/kqueue.c event/kqueue.h \
	event/listener.c event/listener.h \
	event/nonblock.c event/nonblock.h \
	event/selfpipe.c event/selfpipe.h \
	fiber/combiner.c fiber/combiner.h \
	fiber/fiber.c fiber/fiber.h \
	fiber/future.c fiber/future.h \
	fiber/runq.c fiber/runq.h \
	fiber/strand.c fiber/strand.h \
	fiber/wait.c fiber/wait.h \
	memory/alloc.c memory/alloc.h \
	memory/arena.h memory/arena.c \
	memory/buffer.c memory/buffer.h \
	memory/cache.c memory/cache.h \
	memory/pool.c memory/pool.h \
	memory/region.c memory/region.h \
	memory/span.c memory/span.h \
	net/address.h net/address.c \
	net/net.c net/net.h \
	net/netbuf.c net/netbuf.h \
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
	arch/x86/cstack.c arch/x86/cstack.h arch/x86/cstack-switch.S \
	arch/x86/intrinsic.h \
	arch/x86/syscall.h arch/x86/syscall.c
endif

if ARCH_X86_64
libmainbase_la_SOURCES += \
	arch/x86-64/asm.h \
	arch/x86-64/atomic.h \
	arch/x86-64/basic.h \
	arch/x86-64/cstack.c arch/x86-64/cstack.h \
	arch/x86-64/intrinsic.h \
	arch/x86-64/syscall.h arch/x86-64/syscall.c
endif

if ARCH_GENERIC
libmainbase_la_SOURCES += \
	arch/generic/atomic.h \
	arch/generic/basic.h \
	arch/generic/cstack.c arch/generic/cstack.h \
	arch/generic/intrinsic.h \
	arch/generic/syscall.h
endif

libmainbase_la_LIBADD = libcksum.la
