project('libass',
        'c',
        license: ['ISC'],
        version: '0.17.1',
        meson_version: '>=0.64.0',
        default_options: [
            'c_std=gnu99',
        ]
)
cc = meson.get_compiler('c')

conf = configuration_data()

iconv_dep = dependency('iconv', required: false)
m_dep = cc.find_library('m', required : false)
freetype_dep = dependency('freetype', version: '>= 2.0.0')
fribidi_dep = dependency('fribidi', version: '>= 0.19.1')
harfbuzz_dep = dependency('harfbuzz', version: '>= 1.2.3')
libpng_dep = dependency('', required: false)
libunibreak_dep = dependency('libunibreak', version: '>= 1.1', required: get_option('libunibreak'))
fontconfig_dep = dependency('fontconfig', version: '>= 2.10.92', required: get_option('fontconfig'))
dwrite_dep = cc.find_library('dwrite', required: get_option('directwrite'))

if iconv_dep.found() and (cc.has_function('libiconv_open', dependencies: iconv_dep) or cc.has_function('iconv_open', dependencies: iconv_dep))
    conf.set('CONFIG_ICONV', 1)
endif

if not cc.has_function('lrint', dependencies: m_dep)
    error('Unable to locate math functions!')
endif

if freetype_dep.found()
    conf.set('CONFIG_FREETYPE', 1)
endif

if fribidi_dep.found()
    conf.set('CONFIG_FRIBIDI', 1)
endif

if harfbuzz_dep.found()
    conf.set('CONFIG_HARFBUZZ', 1)
endif

if get_option('test').enabled() or get_option('compare').enabled()
    libpng_dep = dependency('libpng', version: '>= 1.2.0', required: false)
    if libpng_dep.found()
        conf.set('CONFIG_LIBPNG', 1)
    else
        error('libpng is required for tests and comparisons')
    endif
endif

if libunibreak_dep.found() and not get_option('libunibreak').disabled()
    conf.set('CONFIG_UNIBREAK', 1)
endif

if fontconfig_dep.found() and not get_option('fontconfig').disabled()
    conf.set('CONFIG_FONTCONFIG', 1)
endif

coretext_deps = []
if not get_option('coretext').disabled()
  app_services_dep = dependency('appleframeworks', modules: ['ApplicationServices'], required: false)
  if cc.has_type('CTFontRef', prefix: '#include <ApplicationServices/ApplicationServices.h>', dependencies: app_services_dep)
    coretext_deps += [app_services_dep]
  # On iOS CoreText and CoreGraphics are stand-alone frameworks
  # Check for a different symbol to avoid getting cached result
  else
    coretext_dep = dependency('appleframeworks', modules: ['CoreText'], required: false)
    coregraphics_dep = dependency('appleframeworks', modules: ['CoreGraphics'], required: false)
    corefoundation_dep = dependency('appleframeworks', modules: ['CoreFoundation'], required: false)
    if cc.has_type('CTRunRef', prefix: '#include <CoreText/CoreText.h>', dependencies: [coretext_dep, coregraphics_dep, corefoundation_dep])
      coretext_deps += [coretext_dep, coregraphics_dep, corefoundation_dep]
    elif get_option('coretext').enabled()
      error('CoreText was enabled explicitly, but required headers or frameworks are missing.')
    endif
  endif
endif

if coretext_deps.length() > 0
  conf.set('CONFIG_CORETEXT', 1)
endif

if dwrite_dep.found()
  conf.set('CONFIG_DIRECTWRITE', 1)
endif

if get_option('require_system_font_provider') and not fontconfig_dep.found() and coretext_deps.length() == 0 and not dwrite_dep.found()
  error('Either DirectWrite (on Windows), CoreText (on OSX), or Fontconfig (Linux, other) is required. If you really want to compile without a system font provider, set "require_system_font_provider" to false')
endif



cpu_family = 'unknown'
bits = 0

os = host_machine.system()
if host_machine.cpu_family() == 'x86'
  cpu_family = 'x86'
  bits = 32
elif host_machine.cpu_family() == 'x86_64'
  cpu_family = 'x86'
  bits = 64
elif host_machine.cpu_family() == 'aarch64'
  cpu_family = 'arm'
  bits = 64
endif

asm = false
as = cc
asm_args = ['-Dprivate_prefix=ass']

if not get_option('asm').disabled()
  if cpu_family == 'x86'
    nasm = add_languages('nasm', required: false)
    message(nasm)
    if nasm
      if bits == 32
        asm_args += ['-DARCH_X86_64=0']
      elif bits == 64
        asm_args += ['-DARCH_X86_64=1']
      endif
      if os == 'darwin'
        asm_args += ['-DPREFIX']
        asm_args += ['-DSTACK_ALIGNMENT=16']
      elif os == 'linux' or os == 'android' or os == 'sunos' or os == 'haiku' or os == 'gnu'
        asm_args += ['-DSTACK_ALIGNMENT=16']
      endif
      as = meson.get_compiler('nasm')
      asm = as.compiles('vpmovzxwd ymm0, xmm0', args: asm_args)
      if not asm
        ver = asm.version()
        warning(f'nasm is too old (found {ver}); ASM functions are disabled.')
        warning('Install nasm-2.10 or later for a significantly faster libass build.')
      endif
    endif
  elif cpu_family == 'arm'
    asm = true
    if os == 'darwin'
      asm_args += ['-DPREFIX']
    endif
  else
    warning('Assembly optimizations are not yet supported on this architecture; disabling.' + host_machine.cpu_family())
  endif
endif

if get_option('asm').enabled() and not asm
  error('Assembly was requested, but cannot be built; see prior messages.')
endif

if asm
  conf.set('CONFIG_ASM', 1)
else
  conf.set('CONFIG_ASM', 0)
endif

if get_option('large-tiles').enabled()
  conf.set('CONFIG_LARGE_TILES', 1)
else
  conf.set('CONFIG_LARGE_TILES', 0)
endif

conf.set('CONFIG_SOURCEVERSION', '("@VCS_TAG@")')

subdir('libass')

subdir('ass')
install_headers(libass_headers, subdir: 'ass')

libass_dep = declare_dependency(
	link_with: libass,
	dependencies: libass_deps,
	include_directories: include_directories('.')
)

meson.override_dependency('libass', libass_dep)

pkg = import('pkgconfig')

pkg.generate(
	libass,
	description: 'LibASS is an SSA/ASS subtitles rendering library',
	version: meson.project_version(),
)

if get_option('test').enabled()
  executable('test', 
    files('test'/'test.c'),
    dependencies: [
      libass_dep,
      libpng_dep
    ]
  )
  
endif

if get_option('compare').enabled()
  executable('compare', 
    files(
      'compare'/'image.h', 
      'compare'/'image.c', 
      'compare'/'compare.c'
    ), 
    dependencies: [
      libass_dep,
      libpng_dep
    ]
  )
endif

if get_option('compare').enabled()
  executable('compare', 
    files(
      'compare'/'image.h', 
      'compare'/'image.c', 
      'compare'/'compare.c'
    ), 
    dependencies: [
      libass_dep,
      libpng_dep
    ]
  )
endif