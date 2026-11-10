fuzz_src = files(
    'fuzz.c',
    'writeout.c',
)

add_languages(get_option('fuzz-link-language'), native: false)

libass_fuzz = executable(
    'fuzz',
    fuzz_src,
    install: false,
    include_directories: incs,
    dependencies: deps,
    link_args: get_option('fuzz-link-args'),
    link_language: get_option('fuzz-link-language'),
    link_with: libass_for_tools,
)

# Second build of the harness exercising raw ASS drawing text via the
# internal drawing parser and synthetic dialogues
libass_fuzz_drawing = executable(
    'fuzz_drawing',
    fuzz_src,
    install: false,
    include_directories: incs,
    dependencies: deps,
    c_args: ['-DASSFUZZ_DRAWING_INPUT=1'],
    cpp_args: ['-DASSFUZZ_DRAWING_INPUT=1'],
    link_args: get_option('fuzz-link-args'),
    link_language: get_option('fuzz-link-language'),
    link_with: libass_for_tools,
)

art_samples = get_option('art-samples')
if art_samples != ''
    dir = join_paths(art_samples, 'crash')
    test('art-fuzz', files(join_paths(dir, 'run-all.sh'))[0], workdir: dir, args: [libass_fuzz], depends: [libass_fuzz])
endif
