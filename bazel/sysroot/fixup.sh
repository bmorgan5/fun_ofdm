#!/usr/bin/env bash
# Turns the unpacked contents of some .deb packages into a self-contained
# sysroot: only the target's headers and libraries, with no links out of it.
#
# Usage: fixup.sh <sysroot> <multiarch tuple>
set -euo pipefail

sysroot=$(realpath "$1")
multiarch=$2

# Absolute symlinks would point into the build machine; point them into the
# sysroot.
find "$sysroot" -type l -lname '/*' -print0 | while IFS= read -r -d '' link; do
    ln --symbolic --force --relative "$sysroot$(readlink "$link")" "$link"
done

# Only headers and libraries for the target belong in the sysroot.
find "$sysroot" ! -type d \
    ! -path "$sysroot/usr/include/*" \
    ! -path "$sysroot/usr/lib/$multiarch/*" \
    ! -path "$sysroot/usr/lib/gcc/$multiarch/*" \
    ! -path "$sysroot/usr/lib64/*" \
    -delete
find "$sysroot" -mindepth 1 -depth -type d -empty -delete

# Drop links to files that were left out; Bazel rejects dangling symlinks.
find "$sysroot" -xtype l -delete

# Linker scripts (e.g. libc.so) name libraries by absolute path; the linker
# resolves those inside the sysroot. Debian's are written for a merged /usr
# but still use /lib and /lib64, which the sysroot only has under /usr.
{ grep --recursive --files-with-matches --null --include='*.so' 'GNU ld script' "$sysroot/usr/lib" || true; } |
    xargs -0 --no-run-if-empty sed --in-place --regexp-extended 's#([ (])/lib(64)?/#\1/usr/lib\2/#g'
