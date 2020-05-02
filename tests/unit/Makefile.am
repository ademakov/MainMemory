
AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = -Wall -Wextra

LDADD = $(top_builddir)/src/base/libmainbase.la

TESTS = bitops-test bitset-test json-reader-test scan-test

check_PROGRAMS = $(TESTS)

bitops_test_SOURCES = bitops-test.c
bitset_test_SOURCES = bitset-test.c
json_reader_test_SOURCES = json-reader-test.c
scan_test_SOURCES = scan-test.c
