config_h = vcs_tag(
  input : configure_file(
    output : 'config.h.in',
    configuration : conf,
  ),
  output : 'config.h',
)

libass_deps = [ 
    iconv_dep,
    m_dep,
    freetype_dep,
    fribidi_dep,
    harfbuzz_dep,
    libunibreak_dep,
    fontconfig_dep,
    dwrite_dep,
] + coretext_deps

libass_source = files(
    'ass_utils.h', 'ass_utils.c',
    'ass_string.h', 'ass_string.c',
    'ass_compat.h', 'ass_strtod.c',
    'ass_filesystem.h', 'ass_filesystem.c',
    'ass_types.h', 'ass.h', 'ass_priv.h', 'ass.c',
    'ass_library.h', 'ass_library.c',
    'ass_cache_template.h', 'ass_cache.h', 'ass_cache.c',
    'ass_font.h', 'ass_font.c',
    'ass_fontselect.h', 'ass_fontselect.c',
    'ass_parse.h', 'ass_parse.c',
    'ass_shaper.h', 'ass_shaper.c',
    'ass_outline.h', 'ass_outline.c',
    'ass_drawing.h', 'ass_drawing.c',
    'ass_bitmap.h', 'ass_bitmap.c', 'ass_blur.c',
    'ass_rasterizer.h', 'ass_rasterizer.c',
    'ass_render.h', 'ass_render.c', 'ass_render_api.c',
    'ass_bitmap_engine.h', 'ass_bitmap_engine.c',
    'c/rasterizer_template.h', 'c/c_rasterizer.c',
    'c/c_blend_bitmaps.c',
    'c/c_be_blur.c',
    'c/blur_template.h', 'c/c_blur.c',
    'wyhash.h'
)

libass_source += [config_h]

libass_asm_sources = files()

if asm
    if cpu_family == 'x86'
        libass_asm_sources = files(
            'x86/rasterizer.asm',
            'x86/blend_bitmaps.asm',
            'x86/be_blur.asm',
            'x86/blur.asm',
            'x86/cpuid.h', 'x86/cpuid.asm')
    elif cpu_family == 'arm' and bits == 64
        libass_asm_sources = files(
            'aarch64/rasterizer.S',
            'aarch64/blend_bitmaps.S',
            'aarch64/be_blur.S',
            'aarch64/blur.S',
            'aarch64/asm.S',
        )
    endif
endif

asm_c_args = []
asm_nasm_args = []

if cpu_family == 'x86'
    asm_nasm_args += asm_args
elif cpu_family == 'arm'
    asm_c_args += asm_args
endif

libass_asm = static_library(
    'ass_asm', libass_asm_sources,
    c_args: asm_c_args,
    nasm_args: asm_nasm_args,
    pic: true,
    build_by_default: false,
    include_directories: include_directories('.')
)

if fontconfig_dep.found()
    libass_source += files('ass_fontconfig.h', 'ass_fontconfig.c')
endif

if dwrite_dep.found()
    libass_source += files(
        'dwrite_c.h',
        'ass_directwrite_info_template.h',
        'ass_directwrite.h', 'ass_directwrite.c'
    )
endif

if dwrite_dep.found()
    libass_source += files(
        'dwrite_c.h',
        'ass_directwrite_info_template.h',
        'ass_directwrite.h', 'ass_directwrite.c'
    )
endif

if coretext_deps.length() > 0
    libass_source += files('ass_coretext.h', 'ass_coretext.c')
endif

libass_headers = files('ass_types.h', 'ass.h')

link_args = []

gcc_sym = 'libass.gcc.sym'
clang_sym = 'libass.sym'
msvc_sym = 'libass.def'

gcc_link_args = [ '-Wl,--version-script=' +  meson.current_source_dir() / gcc_sym ]
clang_link_args = [ '-Wl,-exported_symbols_list',  meson.current_source_dir() / clang_sym ]
msvc_link_args= [ '/DEF:' +  meson.current_source_dir() / msvc_sym ]

if cc.has_link_argument(gcc_link_args[0])
    link_args += gcc_link_args
    link_dep = files(gcc_sym)
elif cc.has_multi_link_arguments(clang_link_args[0], clang_link_args[1])
    link_args += clang_link_args
    link_dep = files(clang_sym)
elif cc.has_link_argument(msvc_link_args[0])
    link_args += msvc_link_args
    link_dep = files(msvc_sym)
else
    error('Unsupported linker')
endif


libass = library('ass', libass_source,
    dependencies: libass_deps,
    link_args: link_args,
    link_depends: link_dep,
    link_with: libass_asm)
