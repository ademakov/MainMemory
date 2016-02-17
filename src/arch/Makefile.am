
AM_CFLAGS = -Wall -Wextra

noinst_LTLIBRARIES = libmmarch.la

libmmarch_la_SOURCES = \
	atomic.h basic.h cstack.h \
	lock.h memory.h spin.h \
	syscall.h

if ARCH_X86
libmmarch_la_SOURCES += \
	x86/asm.h x86/atomic.h x86/basic.h \
	x86/cstack.c x86/cstack-switch.S \
	x86/fence.h x86/lock.h x86/spin.h \
	x86/syscall.h x86/syscall.c
endif

if ARCH_X86_64
libmmarch_la_SOURCES += \
	x86-64/asm.h x86-64/atomic.h x86-64/basic.h \
	x86-64/cstack.c x86-64/cstack-switch.S \
	x86-64/fence.h x86-64/lock.h x86-64/spin.h \
	x86-64/syscall.h x86-64/syscall.c
endif

if ARCH_GENERIC
libmmarch_la_SOURCES += \
	generic/atomic.h generic/basic.h \
	generic/cstack.c \
	generic/lock.h generic/spin.h \
	generic/syscall.h
endif
