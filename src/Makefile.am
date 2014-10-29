
SUBDIRS = memcache

bin_PROGRAMS = mmem

mmem_CFLAGS = -Wall -Wextra -DMSPACES -DUSE_DL_PREFIX

arch_sources = \
	arch/atomic.h arch/basic.h arch/lock.h \
	arch/memory.h arch/spin.h arch/stack.h 

base_sources = \
	base/backoff.c base/backoff.h \
	base/barrier.c base/barrier.h \
	base/bitops.h \
	base/bitset.c base/bitset.h \
	base/combiner.c base/combiner.h \
	base/hash.c base/hash.h \
	base/list.h \
	base/log/debug.c base/log/debug.h \
	base/log/error.c base/log/error.h \
	base/log/log.c base/log/log.h \
	base/log/plain.c base/log/plain.h \
	base/log/trace.c base/log/trace.h \
	base/lock.c base/lock.h \
	base/mem/alloc.c base/mem/alloc.h \
	base/mem/arena.h base/mem/arena.c \
	base/mem/buffer.c base/mem/buffer.h \
	base/mem/cdata.c base/mem/cdata.h \
	base/mem/chunk.c base/mem/chunk.h \
	base/mem/malloc.c base/mem/malloc.h \
	base/mem/mem.c base/mem/mem.h \
	base/mem/space.c base/mem/space.h \
	base/mem/stack.c base/mem/stack.h \
	base/ring.c base/ring.h \
	base/sys/clock.c base/sys/clock.h \
	base/timeq.c base/timeq.h \
	base/thr/thread.c base/thr/thread.h \
	base/util/exit.c base/util/exit.h \
	base/util/format.c base/util/format.h \
	base/util/hook.c base/util/hook.h \
	base/util/libcall.c base/util/libcall.h

core_sources = \
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
	$(arch_sources) $(base_sources) \
	$(core_sources) $(net_sources) \
	common.h main.c

mmem_LDADD = memcache/libmemcache.a

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
