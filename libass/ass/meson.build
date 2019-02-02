# See: https://github.com/mesonbuild/meson/issues/2546
# This hack is needed because the install directory of the libass public headers
# ('ass') is different than the name of the directory ('libass') that they live under
# in the source tree. In the scenario where the libass meson build system is being used
# as a subproject of some parent project, that parent project will need the libass
# headers to be copied into the appropriate 'ass' directory either before or during the
# build process so that they are in an appropriate include path prior to the time that
# the build system starts trying to build the parent project that needs to include
# them.
fs = import('fs')
foreach header : libass_headers
    fake_installed_headers += fs.copyfile(header, fs.name(header))
endforeach
