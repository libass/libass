checkasm_src = files(
    'checkasm.c',
    'rasterizer.c',
    'blend_bitmaps.c',
    'be_blur.c',
    'blur.c',
)

checkasm_src_x86 = files(
    'x86/checkasm.asm',
)
checkasm_src_aarch64 = files(
    'arm/checkasm_64.S',
)

checkasm_asm_sources = []
if generic_cpu_family == 'x86'
    checkasm_src += checkasm_src_x86
elif generic_cpu_family == 'aarch64'
    checkasm_src += checkasm_src_aarch64
endif

libass_checkasm = executable(
    'checkasm',
    checkasm_src + config_h,
    install: false,
    include_directories: incs,
    dependencies: deps,
    objects: libass.extract_all_objects(recursive: true),
    link_with: libass_link_with,
    c_args: asm_args,
    build_by_default: false,
)

test('checkasm', libass_checkasm)
