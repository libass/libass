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
    objects: libass.extract_all_objects(recursive: true),
    link_with: libass_link_with,
)

art_samples = get_option('art-samples')
if art_samples != ''
    dir = join_paths(art_samples, 'regression')
    test('art-compare', files(join_paths(dir, 'run-all.sh'))[0], workdir: dir, args: [libass_compare], depends: [libass_compare])
endif
