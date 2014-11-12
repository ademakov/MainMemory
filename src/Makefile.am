
SUBDIRS = base memcache

bin_PROGRAMS = mmem

AM_CFLAGS = -Wall -Wextra

arch_sources = \
	arch/atomic.h arch/basic.h arch/lock.h \
	arch/memory.h arch/spin.h arch/stack.h 

core_sources = \
	core/combiner.c core/combiner.h \
	core/core.c core/core.h \
	core/future.c core/future.h \
	core/lock.h \
	core/pool.c core/pool.h \
	core/port.c core/port.h \
	core/runq.c core/runq.h \
	core/synch.c core/synch.h \
	core/task.c core/task.h \
	core/timer.c core/timer.h \
	core/value.h \
	core/wait.c core/wait.h \
	core/work.c core/work.h

net_sources = \
	event/event.c event/event.h \
	event/nonblock.c event/nonblock.h \
	net/net.c net/net.h \
	net/netbuf.c net/netbuf.h

mmem_SOURCES = \
	$(arch_sources) \
	$(core_sources) \
	$(net_sources) \
	common.h main.c

mmem_LDADD = memcache/libmemcache.a base/libmmbase.a

if ARCH_X86
mmem_SOURCES += \
	arch/x86/asm.h arch/x86/atomic.h arch/x86/basic.h \
	arch/x86/fence.h arch/x86/lock.h arch/x86/spin.h \
	arch/x86/stack-init.c arch/x86/stack-switch.S
endif

if ARCH_X86_64
mmem_SOURCES += \
	arch/x86-64/asm.h arch/x86-64/atomic.h arch/x86-64/basic.h \
	arch/x86-64/fence.h arch/x86-64/lock.h arch/x86-64/spin.h \
	arch/x86-64/stack-init.c arch/x86-64/stack-switch.S
endif

if ARCH_GENERIC
mmem_SOURCES += \
	arch/generic/atomic.h arch/generic/basic.h \
	arch/generic/lock.h arch/generic/spin.h arch/generic/stack.c
endif
