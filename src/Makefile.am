
#SUBDIRS =

bin_PROGRAMS = mmem

mmem_CFLAGS = -std=gnu99

mmem_SOURCES = \
	arch.h common.h event.c event.h list.h main.c net.c net.h \
	pool.c pool.h port.c port.h sched.c sched.h stack.c stack.h \
	task.c task.h util.c util.h dlmalloc/malloc.c dlmalloc/malloc.h

if ARCH_X86_64
mmem_SOURCES += arch/x86-64/stack-init.c arch/x86-64/stack-switch.S
endif
