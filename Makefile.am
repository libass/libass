ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign
EXTRA_DIST = libass.pc.in Changelog

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libass.pc

if ENABLE_TEST
    test = test
endif

if ENABLE_COMPARE
    compare = compare
endif

if ENABLE_PROFILE
    profile = profile
endif

SUBDIRS = libass $(test) $(compare) $(profile)

