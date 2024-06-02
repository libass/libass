project(
    'libass',
    'c',
    license: 'ISC',
    meson_version: '>= 0.64.0',
    default_options: [
        'c_std=c99',
        'buildtype=debugoptimized',
        'warning_level=2',
        'default_library=static',
    ],
    version: files('RELEASEVERSION'),
)

conf = configuration_data()
deps = []
test_deps = []

host_system = host_machine.system()

# Compiler setup

cc = meson.get_compiler('c')

cc_warnings = []
cc_features = []

if cc.get_id() != 'msvc'
    cc_features += ['-D_POSIX_C_SOURCE=200809L', '-fno-math-errno']
endif

if cc.get_argument_syntax() == 'gcc'
    cc_warnings += [
        '-Wno-sign-compare',
        '-Wno-unused-parameter',
        '-Werror-implicit-function-declaration',
        '-Wstrict-prototypes',
        '-Wpointer-arith',
        '-Wredundant-decls',
        '-Wno-missing-field-initializers',
    ]
endif

add_project_arguments(
    cc.get_supported_arguments(cc_warnings + cc_features),
    language: 'c',
)

# Configuration

str_check_functions = ['strdup', 'strndup']

foreach name : str_check_functions
    if (
        cc.has_function(name)
        and cc.has_header_symbol('string.h', name, args: cc_features)
    )
        conf.set('HAVE_@0@'.format(name.to_upper()), 1)
    endif
endforeach

if (
    cc.has_function('fstat')
    and cc.has_header_symbol(
        'sys/stat.h',
        'fstat',
        args: cc_features,
        prefix: '#include <sys/types.h>',
    )
)
    conf.set('HAVE_FSTAT', 1)
endif

# Dependencies

deps += cc.find_library('m', required: false)

iconv_dep = dependency('iconv', required: false)
if iconv_dep.found()
    deps += iconv_dep
    conf.set('CONFIG_ICONV', 1)
endif

deps += dependency(
    'freetype2',
    version: '>= 9.17.3',
    default_options: ['harfbuzz=disabled'],
)

deps += dependency(
    'fribidi',
    version: '>= 0.19.1',
    default_options: ['docs=false', 'tests=false'],
)

harfbuzz_options = [
    'tests=disabled',
    'cairo=disabled',
    'gobject=disabled',
    'glib=disabled',
    'freetype=disabled',
]
deps += dependency(
    'harfbuzz',
    version: '>= 1.2.3',
    default_options: harfbuzz_options,
)

libunibreak_dep = dependency(
    'libunibreak',
    version: '>= 1.1',
    required: get_option('libunibreak'),
)
if libunibreak_dep.found()
    deps += libunibreak_dep
    conf.set('CONFIG_UNIBREAK', 1)
endif

if get_option('test')
    test_deps += dependency('libpng', version: '>= 1.2.0')
    conf.set('CONFIG_LIBPNG', 1)
endif

font_providers = []

fontconfig_dep = dependency(
    'fontconfig',
    version: '>= 2.10.92',
    required: get_option('fontconfig'),
)
fontconfig = fontconfig_dep.found()
if fontconfig
    deps += fontconfig_dep
    conf.set('CONFIG_FONTCONFIG', 1)
    font_providers += ['Fontconfig']
endif

# CoreText
coretext = false
if not get_option('coretext').disabled()
    appservices_dep = dependency(
        'appleframeworks',
        modules: ['ApplicationServices', 'CoreFoundation'],
        required: false,
    )
    # this intentionally includes a leading newline
    coretext_check = '''
        int main(void) {
            CTFontDescriptorCopyAttribute(NULL, kCTFontNameAttribute);
            return 0;
        }
    '''
    appservices_snippet = (
        '#include <ApplicationServices/ApplicationServices.h>' + coretext_check
    )

    if appservices_dep.found() and cc.compiles(appservices_snippet)
        deps += appservices_dep
        conf.set('CONFIG_CORETEXT', 1)
        coretext = true
        font_providers += ['CoreText']
    else
        coretext_dep = dependency(
            'appleframeworks',
            modules: ['CoreText', 'CoreFoundation'],
            required: false,
        )
        coretext_snippet = '#include <CoreText/CoreText.h>' + coretext_check
        if coretext_dep.found() and cc.compiles(coretext_snippet)
            deps += coretext_dep
            conf.set('CONFIG_CORETEXT', 1)
            coretext = true
            font_providers += ['CoreText']
        endif
    endif
endif

# DirectWrite
directwrite = false
if not get_option('directwrite').disabled()
    if cc.has_header('windows.h', required: false)
        directwrite = true
        conf.set('CONFIG_DIRECTWRITE', 1)
        code = '''#include <windows.h>
            #if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
            #error Win32 desktop APIs are available
            #endif'''
        if cc.compiles(code)
            # WinRT/UWP/app build: GDI and LoadLibrary are unavailable,
            # but DirectWrite is always present
            deps += cc.find_library('dwrite', required: true)
            font_providers += ['DirectWrite (WinRT/UWP)']
        else
            # Win32/desktop build: GDI is always present;
            # DirectWrite is optional but can be loaded via LoadLibrary
            deps += cc.find_library('gdi32', required: true)
            font_providers += ['DirectWrite and GDI (Win32)']
        endif
    endif
endif

if get_option('directwrite').enabled() and directwrite == false
    error(
        'DirectWrite support was requested, but it was not found.',
    )
endif

if get_option('coretext').enabled() and coretext == false
    error(
        'CoreText support was requested, but it was not found.',
    )
endif

if get_option('require-system-font-provider')
    if font_providers.length() == 0
        error(
            'Either DirectWrite (on Windows), CoreText (on OSX), or Fontconfig (Linux, ' +
            'other) is required. If you really want to compile without a system font ' +
            'provider, set -Drequire-system-font-provider=false',
        )
    endif
else
    if font_providers.length() == 0
        font_providers += 'none'
    endif
endif

# ASM
enable_asm = false
# used in libass/meson.build
asm_is_nasm = false
asm_args = []

# ASM architecture variables
asm_option = get_option('asm')
cpu_subfamily = host_machine.cpu_family()
if cpu_subfamily.startswith('x86')
    cpu_family = 'x86'
else
    cpu_family = cpu_subfamily
endif

if not asm_option.disabled()
    if cpu_family == 'x86'
        asm_is_nasm = add_languages(
            'nasm',
            required: false,
            native: false,
        )
        if not asm_is_nasm
            warning(
                'nasm was not found; ASM functions are disabled. Install nasm ' +
                '>= 2.10 for a significantly faster libass build.',
            )
        else
            nasm_ver = meson.get_compiler('nasm').version()
            if nasm_ver.version_compare('< 2.10')
                warning(
                    'nasm is too old (found @0@); ASM functions are disabled. '.format(
                        nasm_ver,
                    ) + 'Install nasm >= 2.10 for a significantly faster libass build.',
                )
                asm_is_nasm = false
            endif
        endif
        enable_asm = asm_is_nasm
        if enable_asm
            conf.set('ARCH_X86', 1)
            nasm_args = ['-Dprivate_prefix=ass', '-DPIC=1']

            if cpu_subfamily == 'x86_64'
                conf.set('ARCH_X86_64', 1)
                nasm_args += '-DARCH_X86_64=1'
            else
                nasm_args += '-DARCH_X86_64=0'
            endif

            if host_system in ['windows', 'cygwin']
                if cpu_subfamily == 'x86'
                    nasm_args += '-DPREFIX'
                endif
            elif host_system == 'darwin'
                nasm_args += ['-DPREFIX', '-DSTACK_ALIGNMENT=16']
            elif host_system in ['linux', 'sunos', 'haiku', 'gnu']
                nasm_args += ['-DSTACK_ALIGNMENT=16']
            elif host_system == 'dragonfly' or host_system.endswith('bsd')
                nasm_args += []
            else
                error(
                    'Please contact libass upstream to figure out if ASM support ' +
                    'for your platform can be added. In the meantime you will need ' +
                    'to use -Dasm=disabled.',
                )
            endif

            add_project_arguments(nasm_args, language: 'nasm')
        endif
    elif cpu_subfamily == 'aarch64'
        enable_asm = true
        conf.set('ARCH_AARCH64', 1)
        if host_system == 'darwin'
            asm_args += '-DPREFIX'
        endif
    else
        warning(
            'Assembly optimizations are not yet supported for the "@0@" architecture; disabling.'.format(
                cpu_subfamily,
            ),
        )
    endif

    if enable_asm
        conf.set('CONFIG_ASM', 1)
    elif asm_option.enabled()
        error(
            'Assembly was requested, but cannot be built; see prior messages.',
        )
    endif
endif

conf.set('CONFIG_LARGE_TILES', get_option('large-tiles').to_int())

conf.set('CONFIG_SOURCEVERSION', '"meson, commit: @VCS_TAG@"')

config_h_in = configure_file(output: 'config.h.in.in', configuration: conf)

config_h_intermediate = vcs_tag(
    command: [
        'git',
        'describe',
        '--tags',
        '--long',
        '--always',
        '--broken',
        '--abbrev=40',
    ],
    fallback: '@VCS_TAG_FALLBACK@',
    input: config_h_in,
    output: 'config.h.in',
)

# Fallback command for older git versions (< 2.13.0) that don't support --broken
config_h = vcs_tag(
    command: [
        'git',
        'describe',
        '--tags',
        '--long',
        '--always',
        '--dirty',
        '--abbrev=40',
    ],
    replace_string: '@VCS_TAG_FALLBACK@',
    fallback: 'failed to determine (>= @0@)'.format(meson.project_version()),
    input: config_h_intermediate,
    output: 'config.h',
)

incs = include_directories('.', 'libass')

subdir('libass')

default_library = get_option('default_library')

if default_library == 'both'
    libass_for_tools = libass.get_static_lib()
else
    libass_for_tools = libass
endif

if get_option('test')
    subdir('test')
endif
if get_option('profile')
    subdir('profile')
endif

# libass.pc
pkg = import('pkgconfig')
pkg.generate(
    libass,
    name: 'libass',
    description: 'libass is an SSA/ASS subtitles rendering library',
)
# `libass_dep` comes from subdir('libass')
meson.override_dependency('libass', libass_dep)

if default_library != 'static' and host_system != 'windows'
    warning(
        'this build does not properly support symbol visibility and the resulting shared lib is not suitable for distribution!',
    )
endif

summary('Font providers', font_providers)
summary('ASM optimizations', enable_asm, bool_yn: true)
