profile_src = files('profile.c')

libass_profile = executable(
    'profile',
    profile_src,
    install: false,
    include_directories: incs,
    dependencies: deps,
    link_with: libass_for_tools,
)
