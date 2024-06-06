# Read library versioning from Automake
# (should we ever reorder those in Automake, it will correctly fail
#  due to undefined variables rather than using bogus versions)
fs = import('fs')
am_lib_file = fs.read('Makefile_library.am').split('\n')
foreach line : am_lib_file
    args = line.split('=')
    name = args[0].strip()
    val = args[1].strip()
    if name == 'LIBASS_LT_CURRENT'
        libass_lt_current = val.to_int()
    elif name == 'LIBASS_LT_REVISION'
        libass_lt_revision = val.to_int()
    elif name == 'LIBASS_LT_AGE'
        libass_lt_age = val.to_int()
        break
    endif
endforeach

libass_so_version = '@0@.@1@.@2@'.format(
    libass_lt_current - libass_lt_age,
    libass_lt_age,
    libass_lt_revision,
)

src_x86 = files(
    'x86/be_blur.asm',
    'x86/blend_bitmaps.asm',
    'x86/blur.asm',
    'x86/cpuid.asm',
    'x86/rasterizer.asm',
)
src_aarch64 = files(
    'aarch64/asm.S',
    'aarch64/be_blur.S',
    'aarch64/blend_bitmaps.S',
    'aarch64/blur.S',
    'aarch64/rasterizer.S',
)
src_fontconfig = files('ass_fontconfig.c')
src_directwrite = files('ass_directwrite.c')
src_coretext = files('ass_coretext.c')

libass_src = files(
    'c/c_be_blur.c',
    'c/c_blend_bitmaps.c',
    'c/c_blur.c',
    'c/c_rasterizer.c',
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

libass_link_with = []

libass_headers = files('ass.h', 'ass_types.h')

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
    elif cpu_subfamily == 'aarch64'
        asm_sources = src_aarch64
    endif

    if asm_is_nasm
        libass_src += asm_sources
    else
        asm_lib = static_library(
            'libass_asm',
            config_h,
            sources: asm_sources,
            c_args: asm_args,
            include_directories: incs,
        )
        libass_link_with += asm_lib
    endif
endif

if host_system == 'windows'
    gen_defs = find_program('../gen_defs.py')
    libass_def = custom_target(
        'libass.def',
        input: ['libass.sym'],
        output: ['libass.def'],
        command: [gen_defs, '@INPUT@', '@OUTPUT@'],
    )
    kwargs = {'vs_module_defs': libass_def}
else
    kwargs = {}
endif

# this is a list of custom targets for copying the libass public headers into the build
# directory. These targets are passed as part of the libass library build step in order
# to enforce the desired build sequence.
fake_installed_headers = []
subdir('ass')

libass = library(
    'ass',
    libass_src,
    config_h,
    link_with: libass_link_with,
    version: libass_so_version,
    # This emulates the GNU libtool compat/current versioning computation
    darwin_versions: [
        '@0@'.format(libass_lt_current + 1),
        '@0@.@1@'.format(libass_lt_current + 1, libass_lt_revision),
    ],
    install: true,
    include_directories: incs,
    dependencies: deps,
    kwargs: kwargs,
)

libass_dep = declare_dependency(
    link_with: libass,
    include_directories: incs,
    dependencies: deps,
    sources: fake_installed_headers,
)

install_headers(libass_headers, subdir: 'ass')
