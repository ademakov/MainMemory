dnl Process this file with autoconf to produce a configure script.

dnl Initialize autoconf.
AC_PREREQ(2.68)
AC_INIT([MainMemory], [0.0], [ademakov@gmail.com])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([src/dlmalloc/malloc.c])
AC_CONFIG_HEADERS([src/config.h])

dnl Initialize automake.
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

dnl Determine the host system type.
AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AR
AM_PROG_AS
AC_PROG_RANLIB

dnl Checks for libraries.

dnl Checks for header files.
AC_CHECK_HEADERS([stdlib.h unistd.h sys/epoll.h sys/event.h])

dnl Checks for typedefs, structures, and compiler characteristics.

dnl Checks for library functions.

dnl Check command line arguments

AC_ARG_ENABLE([debug],
	[AS_HELP_STRING(
		[--enable-debug],
		[enable debug run-time checks (default=no)])],
	[debug="$enableval"], [debug=no])
if test "x$debug" = "xyes"; then
	AC_DEFINE(ENABLE_DEBUG, 1, [Define to 1 to enable debug run-time checks.])
fi

AC_ARG_ENABLE([trace],
	[AS_HELP_STRING([--enable-trace],
		[enable function call trace (default=no)])],
	[trace="$enableval"], [trace=no])
if test "x$trace" = "xyes"; then
	AC_DEFINE(ENABLE_TRACE, 1, [Define to 1 to enable function call trace.])
fi

AC_CONFIG_FILES([
	Makefile
	src/Makefile])
AC_OUTPUT
