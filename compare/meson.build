compare_src = files(
    'compare.c',
    'image.c',
)

libass_compare = executable(
    'compare',
    compare_src,
    install: false,
    include_directories: incs,
    dependencies: deps + png_deps,
    link_with: libass_for_tools,
)

art_samples = get_option('art-samples')
if art_samples != ''
    dir = join_paths(art_samples, 'regression')
    test('art-compare', files(join_paths(dir, 'run-all.sh'))[0], workdir: dir, args: [libass_compare.full_path()], depends: [libass_compare])
endif
