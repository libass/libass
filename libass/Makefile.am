AM_CFLAGS = -std=gnu99 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter \
            -Werror-implicit-function-declaration -Wstrict-prototypes        \
            -Wpointer-arith -Wredundant-decls

LIBASS_LT_CURRENT = 6
LIBASS_LT_REVISION = 0
LIBASS_LT_AGE = 1

yasm_verbose = $(yasm_verbose_$(V))
yasm_verbose_ = $(yasm_verbose_$(AM_DEFAULT_VERBOSITY))
yasm_verbose_0 = @echo "  YASM    " $@;

.asm.lo:
	$(yasm_verbose)$(LIBTOOL) $(AM_V_lt) --mode=compile $(AS) $(ASFLAGS) -o $@ $< -prefer-non-pic

SRC_INTEL = x86/blend_bitmaps.asm x86/cpuid.asm x86/blend_bitmaps.h x86/cpuid.h
SRC_INTEL64 = x86/be_blur.asm x86/be_blur.h
SRC_INTEL_RASTERIZER = x86/rasterizer.asm x86/rasterizer.h

SRC_RASTERIZER = ass_rasterizer.h ass_rasterizer.c ass_rasterizer_c.c

lib_LTLIBRARIES = libass.la
libass_la_SOURCES = ass.c ass_cache.c ass_font.c ass_fontconfig.c ass_render.c \
                    ass_utils.c ass_bitmap.c ass_library.c ass_bitmap.h \
                    ass_cache.h ass_fontconfig.h ass_font.h ass.h \
                    ass_library.h ass_types.h ass_utils.h ass_drawing.c \
                    ass_drawing.h ass_cache_template.h ass_render.h \
                    ass_parse.c ass_parse.h ass_render_api.c ass_shaper.c \
                    ass_shaper.h ass_strtod.c

libass_la_LDFLAGS = -no-undefined -version-info $(LIBASS_LT_CURRENT):$(LIBASS_LT_REVISION):$(LIBASS_LT_AGE)
libass_la_LDFLAGS += -export-symbols $(srcdir)/libass.sym

if RASTERIZER
libass_la_SOURCES += $(SRC_RASTERIZER)
endif

if ASM
if INTEL
libass_la_SOURCES += $(SRC_INTEL)
if RASTERIZER
libass_la_SOURCES += $(SRC_INTEL_RASTERIZER)
endif
if X64
libass_la_SOURCES += $(SRC_INTEL64)
endif
endif
endif

assheadersdir = $(includedir)/ass
dist_assheaders_HEADERS = ass.h ass_types.h

EXTRA_DIST = libass.sym x86/x86inc.asm
