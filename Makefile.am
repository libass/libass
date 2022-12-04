ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = -I$(top_srcdir)/libass
AM_CFLAGS = -std=gnu99 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter \
            -Werror-implicit-function-declaration -Wstrict-prototypes        \
            -Wpointer-arith -Wredundant-decls -Wno-missing-field-initializers\
            -D_GNU_SOURCE

EXTRA_DIST = libass.pc.in Changelog MAINTAINERS ltnasm.sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libass.pc

nasm_verbose = $(nasm_verbose_$(V))
nasm_verbose_ = $(nasm_verbose_$(AM_DEFAULT_VERBOSITY))
nasm_verbose_0 = @echo "  NASM    " $@;

lib_LTLIBRARIES =
noinst_PROGRAMS =
check_PROGRAMS =
TESTS =

include libass/Makefile_library.am
include Makefile_util.am

# Tests
if ENABLE_FUZZ
check: check-art-fuzz
.PHONY: check-art-fuzz
check-art-fuzz: fuzz/fuzz
	@if [ -z '$(ART_SAMPLES)' ] ; then \
		echo "ART_SAMPLES location not set; cannot run regression tests!"; \
	else \
		cd '$(ART_SAMPLES)'/crash/ ; \
		./run-all.sh '$(abs_top_builddir)'/fuzz/fuzz ; \
	fi
endif

if ENABLE_COMPARE
check: check-art-compare
.PHONY: check-art-compare
check-art-compare: compare/compare
	@if [ -z '$(ART_SAMPLES)' ] ; then \
		echo "ART_SAMPLES location not set; cannot run regression tests!"; \
	else \
		cd '$(ART_SAMPLES)'/regression/ ; \
		./run-all.sh '$(abs_top_builddir)'/compare/compare ; \
	fi
endif
