libass_lt_current = 11
libass_lt_revision = 1
libass_lt_age = 2

libass_so_version = '@0@.@1@.@2@'.format(libass_lt_current - libass_lt_age, libass_lt_age, libass_lt_revision)

src_x86 = files(
    'x86/be_blur.asm',
    'x86/blend_bitmaps.asm',
    'x86/blur.asm',
    'x86/cpuid.asm',
    'x86/rasterizer.asm',
)
src_c = files(
    'c/c_be_blur.c',
    'c/c_blend_bitmaps.c',
    'c/c_blur.c',
    'c/c_rasterizer.c',
)
src_arm64 = files(
    'aarch64/asm.S',
    'aarch64/be_blur.S',
    'aarch64/blur.S',
    'aarch64/blend_bitmaps.S',
    'aarch64/rasterizer.S',
)
src_fontconfig = files('ass_fontconfig.c')
src_directwrite = files('ass_directwrite.c')
src_coretext = files('ass_coretext.c')

libass_src = files(
    'ass.c',
    'ass_bitmap.c',
    'ass_bitmap_engine.c',
    'ass_blur.c',
    'ass_cache.c',
    'ass_drawing.c',
    'ass_filesystem.c',
    'ass_font.c',
    'ass_fontselect.c',
    'ass_library.c',
    'ass_outline.c',
    'ass_parse.c',
    'ass_rasterizer.c',
    'ass_render.c',
    'ass_render_api.c',
    'ass_shaper.c',
    'ass_string.c',
    'ass_strtod.c',
    'ass_utils.c',
)

libass_src += src_c

libass_link_with = []

libass_headers = files(
    'ass.h',
    'ass_types.h'
)

if fontconfig
    libass_src += src_fontconfig
endif
if directwrite
    libass_src += src_directwrite
endif
if coretext
    libass_src += src_coretext
endif

if enable_asm
    asm_sources = []
    if cpu_family == 'x86'
        asm_sources = src_x86
    elif cpu_family == 'arm64'
        asm_sources = src_arm64
    endif

    if use_nasm
      libass_src += nasm_gen.process(asm_sources)
    else
      asm_lib = static_library('libass_asm',
                               sources: asm_sources,
                               c_args: asm_args,
                               include_directories: incs)
      libass_link_with += asm_lib
    endif
endif

if host_system == 'windows'
    gen_defs = find_program('../gen_defs.py')
    libass_def = custom_target('libass.def',
                               input: ['libass.sym'],
                               output: ['libass.def'],
                               command: [gen_defs, '@INPUT@', '@OUTPUT@'])
    kwargs = {'vs_module_defs': libass_def}
else
    kwargs = {}
endif

libass = library('ass', libass_src, config_h,
                 link_with: libass_link_with,
                 version: libass_so_version,
                 install: true,
                 include_directories: incs,
                 dependencies: deps,
                 kwargs: kwargs)

libass_dep = declare_dependency(link_with: libass,
                                include_directories: incs,
                                dependencies: deps,
                                sources: config_h)

install_headers(libass_headers, subdir: 'ass')
subdir('ass')
