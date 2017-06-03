AM_CFLAGS = -std=gnu99 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter \
            -Werror-implicit-function-declaration -Wstrict-prototypes        \
            -Wpointer-arith -Wredundant-decls -D_GNU_SOURCE

LIBASS_LT_CURRENT = 9
LIBASS_LT_REVISION = 1
LIBASS_LT_AGE = 0

yasm_verbose = $(yasm_verbose_$(V))
yasm_verbose_ = $(yasm_verbose_$(AM_DEFAULT_VERBOSITY))
yasm_verbose_0 = @echo "  YASM    " $@;

.asm.lo:
	$(yasm_verbose)$(LIBTOOL) $(AM_V_lt) --tag=CC --mode=compile $(AS) $(ASFLAGS) -o $@ $< -prefer-non-pic

SRC_INTEL = x86/blend_bitmaps.asm x86/blur.asm x86/cpuid.asm x86/utils.asm \
            x86/cpuid.h
SRC_INTEL64 = x86/be_blur.asm
SRC_INTEL_RASTERIZER = x86/rasterizer.asm

SRC_RASTERIZER = ass_rasterizer.h ass_rasterizer.c ass_rasterizer_c.c

SRC_DIRECTWRITE = ass_directwrite.c ass_directwrite.h dwrite_c.h
SRC_CORETEXT = ass_coretext.c ass_coretext.h

lib_LTLIBRARIES = libass.la
libass_la_SOURCES = ass.c ass_cache.c ass_font.c ass_fontselect.c ass_render.c \
                    ass_utils.c ass_bitmap.c ass_blur.c ass_library.c ass_bitmap.h \
                    ass_cache.h ass_fontselect.h ass_font.h ass.h \
                    ass_library.h ass_types.h ass_utils.h ass_drawing.c \
                    ass_drawing.h ass_cache_template.h ass_render.h \
                    ass_parse.c ass_parse.h ass_render_api.c ass_shaper.c \
                    ass_shaper.h ass_strtod.c ass_fontconfig.c ass_fontconfig.h \
                    ass_string.h ass_string.c ass_compat.h ass_func_template.h

libass_la_LDFLAGS = -no-undefined -version-info $(LIBASS_LT_CURRENT):$(LIBASS_LT_REVISION):$(LIBASS_LT_AGE)
libass_la_LDFLAGS += -export-symbols $(srcdir)/libass.sym

if DIRECTWRITE
libass_la_SOURCES += $(SRC_DIRECTWRITE)
endif

if CORETEXT
libass_la_SOURCES += $(SRC_CORETEXT)
endif

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
