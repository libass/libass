ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = -I$(top_srcdir)/libass

# Using gnu99 to gain compatibility between const and
# non-const array types without needing to jump to ISO C23.
# Otherwise intended to be compatible with ISO C.
#
# Similarly we only strictly _need_ ISO C APIs, but we’ll use
# some additional APIs from e.g. GNU and POSIX when available.
# Unfortunately defining _POSIX_C_SOURCE may end up hiding additional
# non-POSIX interfaces even if their respective feature macro is set too.
# However, POSIX interfaces are usually implicitly enabled
# with other feature macros, thus omit the POSIX one.
AM_CFLAGS = -std=gnu99 -D_GNU_SOURCE -D_XPLATFORM_SOURCE

EXTRA_DIST = libass.pc.in Changelog MAINTAINERS RELEASEVERSION ltnasm.sh

# Meson build setup
EXTRA_DIST += gen_defs.py meson_options.txt meson.build \
              libass/meson.build libass/ass/meson.build \
              fuzz/meson.build checkasm/meson.build \
              compare/meson.build \
              profile/meson.build test/meson.build

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libass.pc

nasm_verbose = $(nasm_verbose_$(V))
nasm_verbose_ = $(nasm_verbose_$(AM_DEFAULT_VERBOSITY))
nasm_verbose_0 = @echo "  NASM    " $@;

lib_LTLIBRARIES =
noinst_LTLIBRARIES =
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
