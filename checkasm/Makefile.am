AM_CFLAGS = -Wall

check_PROGRAMS = checkasm
checkasm_SOURCES = checkasm.h checkasm.c blend_bitmaps.c be_blur.c
checkasm_CPPFLAGS = -I$(top_srcdir)/libass
checkasm_LDADD = $(top_builddir)/libass/.libs/libass.a
checkasm_LDFLAGS = $(AM_LDFLAGS) -static

nasm_verbose = $(nasm_verbose_$(V))
nasm_verbose_ = $(nasm_verbose_$(AM_DEFAULT_VERBOSITY))
nasm_verbose_0 = @echo "  NASM    " $@;

.asm.o:
	$(nasm_verbose)$(AS) $(ASFLAGS) -I$(top_srcdir)/libass/ -o $@ $<

if INTEL
checkasm_SOURCES += x86/checkasm.asm
endif

run-checkasm: checkasm$(EXEEXT)
	./checkasm$(EXEEXT)

run-checkasm-bench: checkasm$(EXEEXT)
	./checkasm$(EXEEXT) --bench

TESTS = checkasm$(EXEEXT)

bench: run-checkasm-bench
