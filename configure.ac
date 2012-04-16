dnl Process this file with autoconf to produce a configure script.

dnl Initialize autoconf.
AC_PREREQ(2.68)
AC_INIT([MainMemory], [0.0], [ademakov@gmail.com])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([src/mmemd.c])
AC_CONFIG_HEADERS([src/config.h])

dnl Initialize automake.
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

dnl Determine the host system type.
AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB

dnl Checks for libraries.

dnl Checks for header files.
AC_CHECK_HEADERS([stdlib.h unistd.h])

dnl Checks for typedefs, structures, and compiler characteristics.

dnl Define coro flags
case "$host" in
	i[[34567]]86-*-*|x86_64-*-*)
		coro_flags=-DCORO_ASM
		;;
	*)
		coro_flags=-DCORO_SJLJ
		;;
esac
AC_SUBST([CORO_FLAGS], [$coro_flags])

dnl Checks for library functions.

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	src/coro/Makefile])
AC_OUTPUT
