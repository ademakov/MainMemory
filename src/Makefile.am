
SUBDIRS = memcache

bin_PROGRAMS = mmem

mmem_CFLAGS = -Wall -Wextra -DMSPACES -DUSE_DL_PREFIX

mmem_SOURCES = \
	arch/atomic.h arch/common.h arch/memory.h arch/stack.h \
	alloc.c alloc.h bits.h chunk.c chunk.h clock.c clock.h common.h \
	core.c core.h event.c event.h exit.c exit.h future.c future.h \
	hook.c hook.h list.h lock.h log.c log.h main.c net.c net.h \
	pool.c pool.h port.c port.h runq.c runq.h sched.c sched.h \
	stack.c stack.h task.c task.h thread.c thread.h timeq.c timeq.h \
	timer.c timer.h trace.c trace.h util.c util.h work.c work.h \
	dlmalloc/malloc.c dlmalloc/malloc.h

mmem_LDADD = memcache/libmemcache.a

if ARCH_X86
mmem_SOURCES += \
	arch/x86/asm.h arch/x86/atomic.h arch/x86/common.h arch/x86/fence.h \
	arch/x86/stack-init.c arch/x86/stack-switch.S
endif

if ARCH_X86_64
mmem_SOURCES += \
	arch/x86-64/asm.h arch/x86-64/atomic.h arch/x86-64/common.h arch/x86-64/fence.h \
	arch/x86-64/stack-init.c arch/x86-64/stack-switch.S
endif

if ARCH_GENERIC
mmem_SOURCES += arch/generic/atomic.h arch/generic/common.h arch/generic/stack.c
endif
