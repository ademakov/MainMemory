dnl Process this file with autoconf to produce a configure script.

dnl Initialize autoconf.
AC_PREREQ(2.68)
AC_INIT([MainMemory], [0.0], [ademakov@gmail.com])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([src/base/bitops.h])
AC_CONFIG_HEADERS([src/config.h])

dnl Initialize automake.
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

dnl Determine the host system type.
AC_CANONICAL_HOST

AC_MSG_CHECKING([host architecture])
case "$host" in
	i[[3456789]]86-*-*)
		arch=x86
		AC_DEFINE([ARCH_X86], 1, [Build for x86 architecture.])
		;;
	x86_64-*-*|amd64-*-*)
		arch=x86-64
		AC_DEFINE([ARCH_X86_64], 1, [Build for x86-64 architecture.])
		;;
	*)
		arch=generic
		AC_DEFINE([ARCH_GENERIC], 1, [Build for generic architecture.])
		;;
esac
AC_MSG_RESULT([$arch])
AM_CONDITIONAL([ARCH_X86], [test "$arch" = "x86"])
AM_CONDITIONAL([ARCH_X86_64], [test "$arch" = "x86-64"])
AM_CONDITIONAL([ARCH_GENERIC], [test "$arch" = "generic"])

dnl Check for API variants.
AC_GNU_SOURCE
#case "$host" in
#        *-*-darwin*)
#            AC_DEFINE([_XOPEN_SOURCE], 600, [Enable for ucontext.h on Darwin])
#            AC_DEFINE([_DARWIN_C_SOURCE], 1, [Enable for ucontext.h on Darwin])
#            ;;
#esac

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AM_PROG_AR
AM_PROG_AS

dnl Initialize libtool.
LT_INIT([disable-shared])

dnl Checks for libraries.
AC_SEARCH_LIBS([clock_gettime], [rt])
AC_SEARCH_LIBS([pthread_create], [pthread],
        [], [AC_MSG_ERROR(Cannot find pthread library)])

dnl Checks for header files.
AC_CHECK_HEADERS([stdlib.h sysexits.h unistd.h ucontext.h])
AC_CHECK_HEADERS([mach/semaphore.h mach/thread_policy.h])
AC_CHECK_HEADERS([pthread.h pthread_np.h sched.h])
AC_CHECK_HEADERS([sys/param.h sys/cpuset.h], [], [], [[
#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
]])
AC_CHECK_HEADERS([sys/epoll.h sys/eventfd.h sys/event.h])
AC_CHECK_HEADERS([sys/sysctl.h linux/futex.h])
AC_CHECK_HEADERS([mach/mach_time.h sys/time.h time.h]) 
AC_CHECK_HEADERS([xmmintrin.h])

dnl Checks for typedefs, structures, and compiler characteristics.
AX_CHECK_COMPILE_FLAG([-msse4.2], AX_APPEND_FLAG([-msse4.2], [CFLAGS]), [], [], [])

dnl Checks for library functions.
AC_CHECK_FUNCS(pthread_setaffinity_np)
AC_CHECK_FUNCS(pthread_setname_np)
AC_CHECK_FUNCS(pthread_yield_np)
AC_CHECK_FUNCS(pthread_yield)

dnl Check command line arguments

AC_ARG_ENABLE([smp],
	[AS_HELP_STRING([--enable-smp], [enable SMP support (default=yes)])],
	[smp="$enableval"], [smp=yes])
if test "x$smp" = "xyes"; then
	AC_DEFINE([ENABLE_SMP], 1, [Define to 1 to enable SMP support.])
fi

AC_ARG_ENABLE([inline-syscalls],
	[AS_HELP_STRING([--enable-inline-syscalls], [enable inline syscalls (default=yes)])],
	[inline_syscalls="$enableval"], [inline_syscalls=yes])
if test "x$inline_syscalls" = "xyes"; then
	AC_DEFINE([ENABLE_INLINE_SYSCALLS], 1, [Define to 1 to enable inline syscalls.])
fi

AC_ARG_ENABLE([event-stats],
	[AS_HELP_STRING([--enable-event-stats], [enable event statistics (default=no)])],
	[event_stats="$enableval"], [event_stats=no])
if test "x$event_stats" = "xyes"; then
	AC_DEFINE([ENABLE_EVENT_STATS], 1, [Define to 1 to enable event statistics.])
fi

AC_ARG_ENABLE([lock-stats],
	[AS_HELP_STRING([--enable-lock-stats], [enable lock statistics (default=no)])],
	[lock_stats="$enableval"], [lock_stats=no])
if test "x$lock_stats" = "xyes"; then
	AC_DEFINE([ENABLE_LOCK_STATS], 1, [Define to 1 to enable lock statistics.])
fi

AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug], [enable debug run-time checks (default=no)])],
	[debug="$enableval"], [debug=no])
if test "x$debug" = "xyes"; then
	AC_DEFINE([ENABLE_DEBUG], 1, [Define to 1 to enable debug run-time checks.])
fi

AC_ARG_ENABLE([trace],
	[AS_HELP_STRING([--enable-trace], [enable function call trace (default=no)])],
	[trace="$enableval"], [trace=no])
if test "x$trace" = "xyes"; then
	AC_DEFINE([ENABLE_TRACE], 1, [Define to 1 to enable function call trace.])
fi

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	src/base/Makefile
	src/memcache/Makefile
	tests/Makefile
	tests/base/Makefile
	tests/unit/Makefile
	examples/Makefile
	examples/hello_server/Makefile
	examples/proxy_server/Makefile])
AC_OUTPUT
