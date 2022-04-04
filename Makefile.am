ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = -std=gnu99 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter \
            -Werror-implicit-function-declaration -Wstrict-prototypes        \
            -Wpointer-arith -Wredundant-decls -Wno-missing-field-initializers\
            -D_GNU_SOURCE

EXTRA_DIST = libass.pc.in Changelog MAINTAINERS ltnasm.sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libass.pc

lib_LTLIBRARIES =
noinst_PROGRAMS =

include libass/Makefile_library.am
include Makefile_util.am
