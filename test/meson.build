test_src = files('test.c')

libass_test = executable('test', test_src,
                         install: false,
                         include_directories: incs,
                         dependencies: deps + test_deps,
                         link_with: libass)
