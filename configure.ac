AC_INIT(libass, 0.17.1)
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_MACRO_DIR([m4])
LT_INIT
AC_CONFIG_SRCDIR([libass/ass.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AS

# Checks for header files.
AC_CHECK_HEADERS_ONCE([iconv.h])

# Checks for library functions.
AC_CHECK_FUNCS([strdup strndup])

# Query configuration parameters and set their description
AC_ARG_ENABLE([test], AS_HELP_STRING([--enable-test],
    [enable test program (requires libpng) @<:@default=no@:>@]))
AC_ARG_ENABLE([compare], AS_HELP_STRING([--enable-compare],
    [enable compare program (requires libpng) @<:@default=no@:>@]))
AC_ARG_ENABLE([profile], AS_HELP_STRING([--enable-profile],
    [enable profiling program @<:@default=no@:>@]))
AC_ARG_ENABLE([fuzz], AS_HELP_STRING([--enable-fuzz],
    [enable fuzzing consumer @<:@default=no@:>@]))
AC_ARG_ENABLE([fontconfig], AS_HELP_STRING([--disable-fontconfig],
    [disable fontconfig support @<:@default=check@:>@]))
AC_ARG_ENABLE([directwrite], AS_HELP_STRING([--disable-directwrite],
    [disable DirectWrite support (Windows only) @<:@default=check@:>@]))
AC_ARG_ENABLE([coretext], AS_HELP_STRING([--disable-coretext],
    [disable CoreText support (OSX only) @<:@default=check@:>@]))
AC_ARG_ENABLE([libunibreak], AS_HELP_STRING([--disable-libunibreak],
    [disable libunibreak support @<:@default=check@:>@]))
AC_ARG_ENABLE([require-system-font-provider], AS_HELP_STRING([--disable-require-system-font-provider],
    [allow compilation even if no system font provider was found @<:@default=enabled:>@]))
AC_ARG_ENABLE([asm], AS_HELP_STRING([--disable-asm],
    [disable compiling with ASM @<:@default=check@:>@]))
AC_ARG_ENABLE([large-tiles], AS_HELP_STRING([--enable-large-tiles],
    [use larger tiles in the rasterizer (better performance, slightly worse quality) @<:@default=disabled@:>@]))

AC_ARG_VAR([ART_SAMPLES],
    [Path to the root of libass' regression testing sample repository. If set, it is used in make check.])
AC_ARG_VAR([FUZZ_LDFLAGS],
    [Optional special linking flags only used for the fuzzer binary.])
AC_ARG_VAR([FUZZ_CPPFLAGS],
    [If fuzzing program is enabled, set this to select alternative modes; see fuzzer source for options.])
FUZZ_CPPFLAGS="${FUZZ_CPPFLAGS:--DASS_FUZZMODE=0}"

# Checks for available libraries and define corresponding C Macros
# Start with system libs, then check everything else via pkg-config
AS_IF([test "x$ac_cv_header_iconv_h" = xyes], [
    ## Some iconv libraries like GNU's libiconv define iconv_open as a macro to
    ## libiconv_open. As SEARCH_LIBS tests linking not compilation, check for
    ## libiconv_open first. SEARCH_LIBS is smart enough to not add -liconv a second
    ## time in case both versions are defined in the local libiconv.
    use_libiconv=false
    AC_SEARCH_LIBS([libiconv_open], [iconv], [
        use_libiconv=true
    ])
    AC_SEARCH_LIBS([iconv_open], [iconv], [
        use_libiconv=true
    ])
    AS_IF([test "x$use_libiconv" = xtrue], [
        AC_DEFINE(CONFIG_ICONV, 1, [use iconv])
    ])
])
# Locate math functions. Most systems have it either in libc or libm, but a few
# have some, eg C89, functions in libc and others in libm. Use C99 lrint to probe.
AC_SEARCH_LIBS([lrint], [m], [
    # noop
], [
    AC_MSG_ERROR([Unable to locate math functions!])
])
pkg_libs="$LIBS"

## Check for libraries via pkg-config and add to pkg_requires as needed
PKG_CHECK_MODULES([FREETYPE], [freetype2 >= 9.17.3], [
    pkg_requires="freetype2 >= 9.17.3"
    CFLAGS="$CFLAGS $FREETYPE_CFLAGS"
    LIBS="$LIBS $FREETYPE_LIBS"
    AC_DEFINE(CONFIG_FREETYPE, 1, [found freetype2 via pkg-config])
])

PKG_CHECK_MODULES([FRIBIDI], [fribidi >= 0.19.1], [
    pkg_requires="fribidi >= 0.19.1, ${pkg_requires}"
    CFLAGS="$CFLAGS $FRIBIDI_CFLAGS"
    LIBS="$LIBS $FRIBIDI_LIBS"
    AC_DEFINE(CONFIG_FRIBIDI, 1, [found fribidi via pkg-config])
])

PKG_CHECK_MODULES([HARFBUZZ], [harfbuzz >= 1.2.3], [
    pkg_requires="harfbuzz >= 1.2.3, ${pkg_requires}"
    CFLAGS="$CFLAGS $HARFBUZZ_CFLAGS"
    LIBS="$LIBS $HARFBUZZ_LIBS"
    AC_DEFINE(CONFIG_HARFBUZZ, 1, [found harfbuzz via pkg-config])
])

libpng=false
AS_IF([test "x$enable_test" = xyes || test "x$enable_compare" = xyes], [
    PKG_CHECK_MODULES([LIBPNG], [libpng >= 1.2.0], [
        # Only used for test programs, must not be used for distribution
        CFLAGS="$CFLAGS $LIBPNG_CFLAGS"
        AC_DEFINE(CONFIG_LIBPNG, 1, [found libpng via pkg-config])
        libpng=true
    ])
])

AS_IF([test "x$enable_libunibreak" != xno], [
    PKG_CHECK_MODULES([LIBUNIBREAK], [libunibreak >= 1.1], [
        pkg_requires="libunibreak >= 1.1, ${pkg_requires}"
        CFLAGS="$CFLAGS $LIBUNIBREAK_CFLAGS"
        LIBS="$LIBS $LIBUNIBREAK_LIBS"
        AC_DEFINE(CONFIG_UNIBREAK, 1, [found libunibreak via pkg-config])
    ], [
        AS_IF([test "x$enable_libunibreak" = xyes], [
            AC_MSG_ERROR([libunibreak support was requested, but it was not found.])
        ])
    ])
])

## Check for system font providers
### Fontconfig
AS_IF([test "x$enable_fontconfig" != xno], [
    PKG_CHECK_MODULES([FONTCONFIG], [fontconfig >= 2.10.92], [
        pkg_requires="fontconfig >= 2.10.92, ${pkg_requires}"
        CFLAGS="$CFLAGS $FONTCONFIG_CFLAGS"
        LIBS="$LIBS $FONTCONFIG_LIBS"
        AC_DEFINE(CONFIG_FONTCONFIG, 1, [found fontconfig via pkg-config])
        fontconfig=true
    ], [
        fontconfig=false
        AS_IF([test "x$enable_fontconfig" = xyes], [
            AC_MSG_ERROR([fontconfig support was requested, but it was not found.])
        ])
    ])
])

### Coretext
AS_IF([test "x$enable_coretext" != xno], [
    # Linking to CoreText directly only works from Mountain Lion and iOS.
    # In earlier OS X releases CoreText was part of the ApplicationServices
    # umbrella framework.
    AC_MSG_CHECKING([for CORETEXT])
    AC_COMPILE_IFELSE([
        AC_LANG_PROGRAM( dnl# First test for legacy include
            [[#include <ApplicationServices/ApplicationServices.h>]],
            [[CTFontDescriptorCopyAttribute(NULL, kCTFontNameAttribute);]]
        )
    ], [
        pkg_libs="$pkg_libs -framework ApplicationServices -framework CoreFoundation"
        LIBS="$LIBS -framework ApplicationServices -framework CoreFoundation"
        AC_DEFINE(CONFIG_CORETEXT, 1, [found CoreText in ApplicationServices framework])
        coretext=true
        AC_MSG_RESULT([yes])
    ], [
        AC_COMPILE_IFELSE([
            AC_LANG_PROGRAM( dnl# Otherwise check newer include style
                [[#include <CoreText/CoreText.h>]],
                [[CTFontDescriptorCopyAttribute(NULL, kCTFontNameAttribute);]]
            )
        ], [
            pkg_libs="$pkg_libs -framework CoreText -framework CoreFoundation"
            LIBS="$LIBS -framework CoreText -framework CoreFoundation"
            AC_DEFINE(CONFIG_CORETEXT, 1, [found CoreText framework])
            coretext=true
            AC_MSG_RESULT([yes])
        ], [
            coretext=false
            AC_MSG_RESULT([no])
            AS_IF([test "x$enable_coretext" = xyes], [
                AC_MSG_ERROR([CoreText support was requested, but it was not found.])
            ])
        ])
    ])
])

### DirectWrite
AS_IF([test "x$enable_directwrite" != xno], [
    # Linking to DirectWrite directly only works from Windows
    AC_MSG_CHECKING([for DIRECTWRITE])
    AC_COMPILE_IFELSE([
        AC_LANG_PROGRAM([[#include <windows.h>]], [[;]])
    ], [
        directwrite=true
        AC_MSG_RESULT([yes])
        AC_MSG_CHECKING([for Win32 desktop APIs])
        AC_COMPILE_IFELSE([
            AC_LANG_PROGRAM([[
                #include <winapifamily.h>
                #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
                #error Win32 desktop APIs are available
                #endif
            ]], [[;]])
        ], [
            # WinRT/UWP/app build: GDI and LoadLibrary are
            # unavailable, but DirectWrite is always present
            pkg_libs="$pkg_libs -ldwrite"
            LIBS="$LIBS -ldwrite"
            AC_DEFINE(CONFIG_DIRECTWRITE, 1, [found DirectWrite (WinRT/UWP)])
            AC_MSG_RESULT([no])
        ], [
            # Win32/desktop build: GDI is always present;
            # DirectWrite is optional but can be loaded via LoadLibrary
            pkg_libs="$pkg_libs -lgdi32"
            LIBS="$LIBS -lgdi32"
            AC_DEFINE(CONFIG_DIRECTWRITE, 1, [found DirectWrite and GDI (Win32)])
            AC_MSG_RESULT([yes])
        ])
    ], [
        directwrite=false
        AC_MSG_RESULT([no])
        AS_IF([test "x$enable_directwrite" = xyes], [
            AC_MSG_ERROR([DirectWrite support was requested, but it was not found.])
        ])
    ])
])

## Require at least one system font provider by default
AS_IF([test "x$enable_require_system_font_provider" != xno  dnl
        && test "x$fontconfig" != xtrue                     dnl
        && test "x$directwrite" != xtrue                    dnl
        && test "x$coretext" != xtrue                       ], [
    AC_MSG_ERROR(m4_text_wrap(m4_normalize([
            Either DirectWrite (on Windows), CoreText (on OSX), or Fontconfig
            (Linux, other) is required. If you really want to compile without
            a system font provider, add --disable-require-system-font-provider]),
        [                  ],
        [No system font provider!],
        [78]
    ))
])


# Locate and configure Assembler appropriately
can_asm=false
cpu_family="other"
AS_IF([test "x$enable_asm" != xno], [
    AS_CASE([$host],
        [i?86-*], [
            AS=nasm
            cpu_family="x86"
            BITS=32
            BITTYPE=32
            ASFLAGS="$ASFLAGS -DARCH_X86_64=0"
        ],
        [x86_64-*-gnux32|amd64-*-gnux32], [
            AS=nasm
            cpu_family="x86"
            BITS=64
            BITTYPE=x32
            ASFLAGS="$ASFLAGS -DARCH_X86_64=1"
        ],
        [x86_64-*|amd64-*], [
            AS=nasm
            cpu_family="x86"
            BITS=64
            BITTYPE=64
            ASFLAGS="$ASFLAGS -DARCH_X86_64=1"
        ],
        [aarch64-*|aarch64_be-*], [
            cpu_family="arm64"
            BITS=64
            BITTYPE=64
        ],
        [ # default
            AC_MSG_NOTICE([Assembly optimizations are not yet supported on this architecture; disabling.])
        ]
    )
    AS_CASE([$cpu_family],
        [x86], [
            AC_CHECK_PROG([nasm_check], [$AS], [yes])
            AS_IF([test "x$nasm_check" != xyes], [
                AC_MSG_WARN(nasm was not found; ASM functions are disabled.)
                AC_MSG_WARN(Install nasm for a significantly faster libass build.)
            ], [
                AS_CASE([$host_os],
                    [darwin*], [
                        ASFLAGS="$ASFLAGS -f macho$BITTYPE -DPREFIX -DSTACK_ALIGNMENT=16"
                    ],
                    [cygwin*|mingw*], [
                        ASFLAGS="$ASFLAGS -f win$BITTYPE"
                        AS_IF([test "x$BITS" = x32], [
                            ASFLAGS="$ASFLAGS -DPREFIX"
                        ])
                    ],
                    [linux*|solaris*|haiku*|gnu*], [
                        ASFLAGS="$ASFLAGS -f elf$BITTYPE -DSTACK_ALIGNMENT=16"
                    ],
                    [dragonfly*|*bsd*], [
                        ASFLAGS="$ASFLAGS -f elf$BITTYPE"
                    ],
                    [ # default
                        AC_MSG_ERROR(m4_text_wrap(m4_normalize([
                                Please contact libass upstream to figure out if ASM
                                support for your platform can be added.
                                In the meantime you will need to use --disable-asm.]),
                            [                  ],
                            [could not identify NASM format for $host_os !],
                            [78]
                        ))
                    ]
                )
                AC_MSG_CHECKING([if $AS supports vpmovzxwd])
                echo "vpmovzxwd ymm0, xmm0" > conftest.asm
                AS_IF([$AS conftest.asm $ASFLAGS -o conftest.o >conftest.log 2>&1], [
                    AC_MSG_RESULT([yes])
                    can_asm=true
                ], [
                    AC_MSG_RESULT([no])
                    VER=`($AS --version || echo no assembler) 2>/dev/null | head -n 1`
                    AC_MSG_WARN([nasm is too old (found $VER); ASM functions are disabled.])
                    AC_MSG_WARN([Install nasm-2.10 or later for a significantly faster libass build.])
                ])
                rm conftest.asm conftest.o > /dev/null 2>&1
            ])
        ],
        [arm64], [
            AS_CASE([$host],
                [*darwin*], [
                    CCASFLAGS="$CCASFLAGS -DPREFIX"
                ]
            )
            can_asm=true
        ]
    )
])

AS_IF([test x"$enable_asm" = xyes && test x"$can_asm" != xtrue], [
    AC_MSG_ERROR([Assembly was requested, but cannot be built; see prior messages.])
])

# Relay config results to output files

## Tell Makefiles which assembler and flags to use
AC_SUBST([ASFLAGS], ["$ASFLAGS"])
AC_SUBST([AS], ["$AS"])

## Relay package configuration to libass.pc.in
AS_IF([test "x$enable_shared" != xno], [
    AC_SUBST([PKG_LIBS_PUBLIC], [])
    AC_SUBST([PKG_REQUIRES_PUBLIC], [])
    AC_SUBST([PKG_LIBS_PRIVATE], [${pkg_libs}])
    AC_SUBST([PKG_REQUIRES_PRIVATE], [${pkg_requires}])
], [
    # Not mentioned by the pkg-config specification, but
    # a common workaround for poor support for mixed builds
    AC_SUBST([PKG_LIBS_PUBLIC], [${pkg_libs}])
    AC_SUBST([PKG_REQUIRES_PUBLIC], [${pkg_requires}])
    AC_SUBST([PKG_LIBS_PRIVATE], [])
    AC_SUBST([PKG_REQUIRES_PRIVATE], [])
])

## Setup conditionals for use in Makefiles
AM_CONDITIONAL([ASM], [test "x$can_asm" = xtrue])
AM_CONDITIONAL([X86], [test "x$cpu_family" = xx86])
AM_CONDITIONAL([X86_64], [test "x$cpu_family" = xx86 && test "x$BITS" = x64])
AM_CONDITIONAL([AARCH64], [test "x$cpu_family" = xarm64])

AM_CONDITIONAL([ENABLE_LARGE_TILES], [test "x$enable_large_tiles" = xyes])

AM_CONDITIONAL([ENABLE_COMPARE], [test "x$enable_compare" = xyes && test "x$libpng" = xtrue])
AM_CONDITIONAL([ENABLE_TEST], [test "x$enable_test" = xyes && test "x$libpng" = xtrue])
AM_CONDITIONAL([ENABLE_PROFILE], [test "x$enable_profile" = xyes])
AM_CONDITIONAL([ENABLE_FUZZ], [test "x$enable_fuzz" = xyes])
# tcc doesn't have the full support of __attribute__((aligned(32)))
AM_CONDITIONAL([ENABLE_CHECKASM], [test "x$can_asm" = xtrue && test "x$GCC" = xyes])

AM_CONDITIONAL([FONTCONFIG], [test "x$fontconfig" = xtrue])
AM_CONDITIONAL([CORETEXT], [test "x$coretext" = xtrue])
AM_CONDITIONAL([DIRECTWRITE], [test "x$directwrite" = xtrue])

## Define C Macros not relating to libraries
AM_COND_IF([ASM], [
    AC_DEFINE(CONFIG_ASM, 1, [ASM enabled])
    AM_COND_IF([X86], [
        AC_DEFINE(ARCH_X86, 1, [targeting a 32- or 64-bit x86 host architecture])
    ])
    AM_COND_IF([X86_64], [
        AC_DEFINE(ARCH_X86_64, 1, [targeting a 64-bit x86 host architecture])
    ])
    AM_COND_IF([AARCH64], [
        AC_DEFINE(ARCH_AARCH64, 1, [targeting a 64-bit arm host architecture])
    ])
], [
    AC_DEFINE(CONFIG_ASM, 0, [ASM enabled])
])

AM_COND_IF([ENABLE_LARGE_TILES], [
    AC_DEFINE(CONFIG_LARGE_TILES, 1, [use large tiles])
], [
    AC_DEFINE(CONFIG_LARGE_TILES, 0, [use small tiles])
])

## Make a guess about the source code version
AS_IF([test -e "${srcdir}/.git"], [
    AC_PATH_PROG([git_bin], [git])
    AS_IF([test -n "$git_bin"], [
        tmp="$("$git_bin" -C "$srcdir" describe --tags --long --always --broken --abbrev=40 2>/dev/null)" \
        || tmp="$("$git_bin" -C "$srcdir" describe --tags --long --always --dirty --abbrev=40 2>/dev/null)" \
        || tmp="failed to determine (>= AC_PACKAGE_VERSION)"
        srcversion_string="commit: $tmp"
    ], [
        srcversion_string="custom after: AC_PACKAGE_VERSION"
    ])
], [
    dnl# Hope no one creates custom tarballs without adjusting the version
    srcversion_string="tarball: AC_PACKAGE_VERSION"
])
AC_DEFINE_UNQUOTED([CONFIG_SOURCEVERSION], ["$srcversion_string"],
                   [string containing info about the used source])

## Setup output beautifier.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_FILES([Makefile libass.pc])
AC_OUTPUT
