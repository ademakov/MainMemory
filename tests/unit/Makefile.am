
AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra

LDADD = $(top_builddir)/src/base/libmmbase.a $(top_builddir)/src/arch/libmmarch.a

TESTS = bitops-test bitset-test json-reader-test

check_PROGRAMS = $(TESTS)

bitops_test_SOURCES = bitops-test.c
bitset_test_SOURCES = bitset-test.c
json_reader_test_SOURCES = json-reader-test.c
