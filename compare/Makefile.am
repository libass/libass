AM_CFLAGS = -Wall

noinst_PROGRAMS = compare
compare_SOURCES = image.h image.c compare.c
compare_CPPFLAGS = -I$(top_srcdir)/libass
compare_LDADD = $(top_builddir)/libass/.libs/libass.a
compare_LDFLAGS = $(AM_LDFLAGS) $(LIBPNG_LIBS) -static
