
SUBDIRS = base memcache

AM_CFLAGS = -Wall -Wextra

bin_PROGRAMS = mmem

arch_sources = \
	arch/atomic.h arch/basic.h arch/cstack.h \
	arch/lock.h arch/memory.h arch/spin.h

if ARCH_X86
arch_sources += \
	arch/x86/asm.h arch/x86/atomic.h arch/x86/basic.h \
	arch/x86/cstack-init.c arch/x86/cstack-switch.S \
	arch/x86/fence.h arch/x86/lock.h arch/x86/spin.h
endif

if ARCH_X86_64
arch_sources += \
	arch/x86-64/asm.h arch/x86-64/atomic.h arch/x86-64/basic.h \
	arch/x86-64/cstack-init.c arch/x86-64/cstack-switch.S \
	arch/x86-64/fence.h arch/x86-64/lock.h arch/x86-64/spin.h
endif

if ARCH_GENERIC
arch_sources += \
	arch/generic/atomic.h arch/generic/basic.h \
	arch/generic/cstack.c \
	arch/generic/lock.h arch/generic/spin.h
endif

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

event_sources = \
	event/batch.c event/batch.h \
	event/dispatch.c event/dispatch.h \
	event/epoll.c event/epoll.h \
	event/event.c event/event.h \
	event/kqueue.c event/kqueue.h \
	event/listener.c event/listener.h \
	event/nonblock.c event/nonblock.h \
	event/selfpipe.c event/selfpipe.h

net_sources = \
	net/net.c net/net.h \
	net/netbuf.c net/netbuf.h

mmem_SOURCES = \
	$(arch_sources) $(core_sources) \
	$(event_sources) $(net_sources) \
	common.h main.c

mmem_LDADD = memcache/libmemcache.a base/libmmbase.a
