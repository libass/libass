AM_CFLAGS = -std=gnu99 -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter \
            -Werror-implicit-function-declaration -Wstrict-prototypes        \
            -Wpointer-arith -Wredundant-decls

LIBASS_LT_CURRENT = 5
LIBASS_LT_REVISION = 0
LIBASS_LT_AGE = 0

.asm.lo:
	$(LIBTOOL) --quiet --mode=compile $(AS) $(ASFLAGS) -o $@ $< -prefer-non-pic

.S.lo:
	$(LIBTOOL) --quiet --mode=compile $(AS) $(ASFLAGS)-o $@ $< -prefer-non-pic

SRC_INTEL = x86/blend_bitmaps.asm x86/cpuid.asm x86/blend_bitmaps.h x86/cpuid.h
SRC_INTEL64 = x86/be_blur.asm x86/be_blur.h

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

if ASM
if INTEL
AS = "yasm"
libass_la_SOURCES += $(SRC_INTEL)
if X86
ASFLAGS += -DARCH_X86_64=0 -m x86
if MACHO
ASFLAGS += -f macho32 -DPREFIX -DHAVE_ALIGNED_STACK=1
endif
if ELF
ASFLAGS += -f elf -DHAVE_ALIGNED_STACK=1
endif
if WIN32
ASFLAGS += -f win32 -DPREFIX -DHAVE_ALIGNED_STACK=0
endif
endif
if X64
libass_la_SOURCES += $(SRC_INTEL64)
ASFLAGS += -DARCH_X86_64=1 -m amd64
if MACHO
ASFLAGS += -f macho64 -DPREFIX -DHAVE_ALIGNED_STACK=1
endif
if ELF
ASFLAGS += -f elf -DHAVE_ALIGNED_STACK=1
endif
if WIN32
ASFLAGS += -f win64 -DHAVE_ALIGNED_STACK=1
endif
endif
endif
endif

assheadersdir = $(includedir)/ass
dist_assheaders_HEADERS = ass.h ass_types.h

EXTRA_DIST = libass.sym x86/x86inc.asm
