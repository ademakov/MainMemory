
SUBDIRS = memcache

bin_PROGRAMS = mmem

mmem_CFLAGS = -std=gnu99 -Wall -Wextra

mmem_SOURCES = \
	arch.h bits.h clock.c clock.h common.h core.c core.h event.c event.h \
	future.c future.h hook.c hook.h list.h main.c net.c net.h \
	pool.c pool.h port.c port.h runq.c runq.h sched.c sched.h \
	stack.c stack.h task.c task.h thread.c thread.h timeq.c timeq.h \
	timer.c timer.h util.c util.h work.c work.h \
	dlmalloc/malloc.c dlmalloc/malloc.h

mmem_LDADD = memcache/libmemcache.a

if ARCH_X86
mmem_SOURCES += arch/x86/stack-init.c arch/x86/stack-switch.S
endif
if ARCH_X86_64
mmem_SOURCES += arch/x86-64/stack-init.c arch/x86-64/stack-switch.S
endif
if ARCH_GENERIC
mmem_SOURCES += arch/generic/stack.c
endif
