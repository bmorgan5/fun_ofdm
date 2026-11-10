#!/usr/bin/env bash
# Runs a binary built for another architecture under qemu's user-mode
# emulation, with that architecture's sysroot as its root. The cross configs
# in .bazelrc use it as --run_under, so `bazel test` and `bazel run` work for
# either architecture on either host. A binary for the host's architecture
# runs directly.
#
# Usage: run.sh <binary or script> [args...]
set -euo pipefail

binary=$1

if ! command -v readelf >/dev/null; then
    echo "run.sh: readelf isn't installed: sudo apt install binutils" >&2
    exit 1
fi

# A script, like an sh_test, runs on the host. It runs its binaries through
# this script itself, as src/bench/bench-smoke-test.sh does.
if ! readelf --file-header "$binary" >/dev/null 2>&1; then
    exec "$@"
fi

# The binary's architecture, from its ELF header.
machine=$(readelf --file-header "$binary" | sed -n 's/^ *Machine: *//p')
case $machine in
    AArch64) arch=aarch64 ;;
    *X86-64) arch=x86_64 ;;
    *)
        echo "run.sh: $binary isn't an aarch64 or x86_64 binary (machine: ${machine:-unknown})." >&2
        exit 1
        ;;
esac

if [[ $arch == "$(uname -m)" ]]; then
    exec "$@"
fi

if ! command -v "qemu-$arch" >/dev/null; then
    echo "run.sh: qemu-$arch isn't installed: sudo apt install qemu-user" >&2
    exit 1
fi

# The binary's own runfiles have the sysroot (//third_party:sysroot). Bazel
# runs it from <runfiles>/_main, and tests also get RUNFILES_DIR.
runfiles=${RUNFILES_DIR:-$PWD/..}
sysroot="$runfiles/+sysroot+sysroot_linux_$arch/sysroot"
if [[ ! -d $sysroot ]]; then
    echo "run.sh: no $arch sysroot in $runfiles: add //third_party:sysroot to the target's data." >&2
    exit 1
fi
exec "qemu-$arch" -L "$sysroot" "$@"
