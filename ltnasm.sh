#!/bin/sh

# Translate libtool supplied C-compiler options for NASM.
# libtool treats NASM like the C compiler, and may supply -f… options
# which are interpreted as the output file format by NASM, causing errors.
# Notably libtool will set -DPIC -fPIC and -fno-common;
# we want to use -DPIC by translating it to -DPIC=1, but remove everything else
#
# Theoretically the way the filtering is done here in a plain POSIX shell script,
# does mess up if there were spaces in any argument. However this will never happen
# since neither our filenames nor options do not contain spaces and source paths
# are not allowed to contain spaces by configure.

cmd=""
while [ "$#" -gt 0 ] ; do
    case "$1" in
        # NASM accepts both -f format and -fformat,
        # we always use the former, and libtool supplied
        # C-compiler options will always use the latter.
        -f) cmd="$cmd $1" ;;
        -f*) : ;;
        -DPIC) cmd="$cmd -DPIC=1" ;;
        *) cmd="$cmd $1" ;;
    esac
    shift
done

exec $cmd
