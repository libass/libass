project('libass', 'c', license: 'ISC',
        meson_version: '>= 0.60.0',
        # cpp_std specified because meson compiler options are currently global, can be set per-subproject with 0.63
        default_options: ['c_std=c99', 'cpp_std=c++11', 'buildtype=debugoptimized', 'warning_level=2', 'default_library=static'],
        version: '0.17.1')

conf = configuration_data()
deps = []
test_deps = []

host_system = host_machine.system()

# Compiler setup

cc = meson.get_compiler('c')

if cc.get_argument_syntax() == 'gcc'
    cflags = [
        '-Wno-sign-compare',
        '-Wno-unused-parameter',
        '-Werror-implicit-function-declaration',
        '-Wstrict-prototypes',
        '-Wpointer-arith',
        '-Wredundant-decls',
        '-Wno-missing-field-initializers'
    ]
    add_project_arguments(cc.get_supported_arguments(cflags), language: 'c')
endif

test_args = []

if cc.get_id() != 'msvc'
    test_args += '-D_POSIX_C_SOURCE=200809L'
    add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
endif

# Configuration

if get_option('large-tiles')
    conf.set('CONFIG_LARGE_TILES', 1)
endif

conf.set('PACKAGE_NAME', 'libass')
conf.set('PACKAGE_VERSION', meson.project_version())

conf.set('CONFIG_SOURCEVERSION', '"meson, commit: @VCS_TAG@"')

str_check_functions = [
    'strdup',
    'strndup'
]

foreach name: str_check_functions
    if cc.has_function(name) and cc.has_header_symbol('string.h', name, args: test_args)
        conf.set('HAVE_@0@'.format(name.to_upper()), 1)
    endif
endforeach

# Dependencies

deps += cc.find_library('m', required: false)

iconv_dep = dependency('iconv', required: false)
if iconv_dep.found()
    deps += iconv_dep
    conf.set('CONFIG_ICONV', 1)
endif

freetype_dep = dependency('freetype2',version: '>= 9.17.3',
                          fallback: ['freetype2', 'freetype_dep'],
                          default_options: ['default_library=' + get_option('default_library'), 'harfbuzz=disabled']) # workaround for https://github.com/mesonbuild/meson/issues/8047
if freetype_dep.found()
    deps += freetype_dep
    conf.set('CONFIG_FREETYPE', 1)
endif

fribidi_dep = dependency('fribidi', version: '>= 0.19.1',
                         fallback: ['fribidi', 'libfribidi_dep'],
                         default_options: ['docs=false', 'tests=false'])
if fribidi_dep.found()
    deps += fribidi_dep
    conf.set('CONFIG_FRIBIDI', 1)
endif

harfbuzz_options = ['tests=disabled', 'cairo=disabled', 'gobject=disabled', 'glib=disabled', 'freetype=disabled']
harfbuzz_dep = dependency('harfbuzz', version: '>= 1.2.3',
                          fallback: ['harfbuzz', 'libharfbuzz_dep'],
                          default_options: harfbuzz_options)
if harfbuzz_dep.found()
    deps += harfbuzz_dep
    conf.set('CONFIG_HARFBUZZ', 1)
endif

libunibreak_dep = dependency('libunibreak', version: '>= 1.1', required: get_option('libunibreak'))
if libunibreak_dep.found()
    deps += libunibreak_dep
    conf.set('CONFIG_LIBUNIBREAK', 1)
endif

fontconfig_dep = dependency('fontconfig', version: '>= 2.10.92', required: get_option('fontconfig'))
fontconfig = fontconfig_dep.found()
if fontconfig
    deps += fontconfig_dep
    conf.set('CONFIG_FONTCONFIG', 1)
endif

if get_option('test')
    test_deps += dependency('libpng', version: '>= 1.2.0')
    conf.set('CONFIG_LIBPNG', 1)
endif

# DirectWrite
directwrite = false
if host_system == 'windows' and not get_option('directwrite').disabled()
    if cc.has_header('windows.h')
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
        else
            # Win32/desktop build: GDI is always present;
            # DirectWrite is optional but can be loaded via LoadLibrary
            deps += cc.find_library('gdi32', required: true)
        endif
    elif get_option('directwrite').enabled()
        error('DirectWrite was enabled explicitly, but required header is missing.')
    endif
endif

# CoreText
coretext = false
if host_system == 'darwin' and not get_option('coretext').disabled()
    coretext_dep = dependency('appleframeworks', modules: ['CoreText', 'CoreFoundation'], required: false)
    appservices_dep = dependency('appleframeworks', modules: ['ApplicationServices', 'CoreFoundation'], required: false)
    if cc.has_type('CTRunRef', prefix: '#include <CoreText/CoreText.h>', dependencies: coretext_dep)
        deps += coretext_dep
        conf.set('CONFIG_CORETEXT', 1)
        coretext = true
    elif cc.has_type('CTRunRef', prefix: '#include <ApplicationServices/ApplicationServices.h>', dependencies: appservices_dep)
        deps += appservices_dep
        conf.set('CONFIG_CORETEXT', 1)
        coretext = true
    elif get_option('coretext').enabled()
        error('CoreText was enabled explicitly, but required headers or frameworks are missing.')
    endif
endif

if get_option('require-system-font-provider') and not fontconfig and not directwrite and not coretext
    error('''Either DirectWrite (on Windows), CoreText (on OSX), or Fontconfig
(Linux, other) is required. If you really want to compile without
a system font provider, set -Drequire-system-font-provider=false''')
endif

# ASM

asm_args = []

# Architecture check
if host_machine.cpu_family() == 'x86'
    bittype = '32'
    asm_args += '-DARCH_X86_64=0'
    cpu_family = 'x86'
elif host_machine.cpu_family() == 'x86_64'
    bittype = '64'
    asm_args += '-DARCH_X86_64=1'
    cpu_family = 'x86'
elif host_machine.cpu_family() == 'aarch64' and host_system == 'darwin'
    bittype = '64'
    cpu_family = 'arm64'
endif

if cpu_family == 'x86'
    nasm = find_program('nasm', required: get_option('asm'))
    enable_asm = nasm.found()
    use_nasm = true
elif cpu_family == 'arm64'
    enable_asm = true
    use_nasm = false
else
    enable_asm = false
    use_nasm = false
endif

# NASM version check
if enable_asm and use_nasm
    # This can be cleaned up once 0.64 is available, see https://github.com/xclaesse/libass/commit/cfd3d9971bd585939b97409c0aac79cab3dd2da5
    nasm_r = run_command(nasm, '-v', check: true)
    out = nasm_r.stdout().strip().split()
    if out[1].to_lower() == 'version'
        if out[2].version_compare('< 2.10')
            warning('nasm is too old (found @0@); ASM functions are disabled.'.format(out[2]))
            enable_asm = false
        endif
    else
        warning('Unexpected nasm version string: @0@; disabling.'.format(nasm_r.stdout()))
        enable_asm = false
    endif
endif

if get_option('asm').enabled() and (not enable_asm)
    error('ASM functions were enabled but are unavailable.')
elif (not enable_asm) and use_nasm and (not get_option('asm').disabled())
    warning('Install nasm-2.10 or later for a significantly faster libass build.')
endif

# System check
if enable_asm
    if use_nasm
        if host_system == 'windows'
            asm_args += ['-f', 'win' + bittype]
            if bittype == '32'
                asm_args += '-DPREFIX'
            endif
        elif host_system == 'darwin'
            asm_args += ['-f', 'macho' + bittype, '-DPREFIX', '-DSTACK_ALIGNMENT=16']
        elif host_system == 'linux' or host_system == 'sunos' or host_system == 'haiku' or host_system == 'gnu'
            asm_args += ['-f', 'elf' + bittype, '-DSTACK_ALIGNMENT=16']
        elif host_system == 'dragonfly' or host_system.endswith('bsd')
            asm_args += ['-f', 'elf' + bittype]
        else
            error('Please contact libass upstream to figure out if ASM support for your platform can be added. In the meantime, disabling.')
            enable_asm = false
        endif
    elif cpu_family == 'arm64'
        asm_args += '-DPREFIX'
    endif
endif

if enable_asm
    conf.set('CONFIG_ASM', 1)
    if cpu_family == 'x86'
        conf.set('ARCH_X86', 1)
    elif cpu_family == 'arm64'
        conf.set('ARCH_AARCH64', 1)
    endif

    if use_nasm
        asm_args += '-Dprivate_prefix=ass'
        asm_args += ['-I', meson.current_source_dir() / 'libass' + '/', '-DPIC=1', '-o', '@OUTPUT@', '@INPUT@']

        nasm_gen = generator(nasm, output: '@BASENAME@.o', arguments: asm_args)
    endif
endif

config_h_in = configure_file(output: 'config.h.in', configuration: conf)

config_h = vcs_tag(command: ['git', 'describe', '--tags', '--long', '--always', '--broken', '--abbrev=40'],
                   fallback: 'failed to determine (>= @0@)'.format(meson.project_version()),
                   input: config_h_in,
                   output: 'config.h')

incs = include_directories('.', 'libass')

subdir('libass')
if get_option('test')
    subdir('test')
endif
if get_option('profile')
    subdir('profile')
endif

# libass.pc
pkg = import('pkgconfig')
pkg.generate(libass,
             name: 'libass',
             description: 'libass is an SSA/ASS subtitles rendering library')
