#!/usr/bin/env bash
# Installs what fun-ofdm needs on Ubuntu 26.04, the same way on a host as in
# the dev container, whose Dockerfile runs it. A release archive includes it.
#
#   sudo bootstrap.sh            # Everything: running, building, developing
#   sudo bootstrap.sh --runtime  # Only what running fun-ofdm needs
#   bootstrap.sh --simulate      # Show what it would install, changing nothing
#
# Running it again skips what's already installed and installs what changed,
# so it's also how to update a host after a change here. --reinstall
# reinstalls everything.

set -euo pipefail

usage() {
    cat <<EOF
Usage: sudo $0 [--runtime] [--reinstall] [-s|--simulate]

Installs what fun-ofdm needs on Ubuntu 26.04: apt packages, and programs
in /usr/local/bin.

  --runtime       Only what running fun-ofdm needs, not building it.
  --reinstall     Reinstall what's already installed, like
                  apt-get --reinstall, instead of skipping it.
  -s, --simulate  Show what it would install, like apt-get --simulate, and
                  change nothing. Doesn't need root.
EOF
}

runtime_only=false
reinstall=false
simulate=false
for arg in "$@"; do
    case $arg in
        --runtime) runtime_only=true ;;
        --reinstall) reinstall=true ;;
        -s | --simulate) simulate=true ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

if ! $simulate && ((EUID != 0)); then
    echo "bootstrap.sh: run it as root: sudo $0 $*" >&2
    exit 1
fi

# What running fun-ofdm needs. It links libuhd dynamically, so it loads it at
# run time. Keep the version in step with the libuhd in the build's sysroot
# (MODULE.bazel).
runtime_packages=(
    libuhd4.9.0
)

# What building and developing need: tools/qemu runs cross-compiled binaries
# with qemu-user and binutils' readelf, and the downloads below need curl.
dev_packages=(
    binutils
    ca-certificates
    curl
    git
    python3
    qemu-user
)

packages=("${runtime_packages[@]}")
if ! $runtime_only; then
    packages+=("${dev_packages[@]}")
fi

apt_options=(--no-install-recommends)
if $reinstall; then
    apt_options+=(--reinstall)
fi

# Starts a section of the output: the apt packages, then the programs this
# installs itself.
section() {
    [[ -z "${in_section:-}" ]] || echo
    in_section=true
    echo "==> $*"
}

section "apt packages: ${packages[*]}"
export DEBIAN_FRONTEND=noninteractive
if $simulate; then
    # apt-get lists the packages it would install, with their dependencies,
    # from the package lists the machine already has.
    apt-get install --simulate "${apt_options[@]}" "${packages[@]}" ||
        echo "bootstrap.sh: apt-get can't install them yet." \
            "A real run updates the package lists first, which might fix that." >&2
else
    apt-get update
    apt-get install -y "${apt_options[@]}" "${packages[@]}"
fi

if $runtime_only; then
    exit 0
fi

arch=$(dpkg --print-architecture)
if [[ "$arch" != amd64 && "$arch" != arm64 ]]; then
    echo "bootstrap.sh: only amd64 and arm64 are supported, not $arch." >&2
    exit 1
fi

# The programs to install in /usr/local/bin, each downloaded from its URL and
# checked against its SHA-256 for this architecture.
programs=()
declare -A program_version program_url program_sha256

# Adds program $1, version $2, from URL $3 with SHA-256 $4.
add_program() {
    programs+=("$1")
    program_version[$1]=$2
    program_url[$1]=$3
    program_sha256[$1]=$4
}

# Bazel, through bazelisk, which runs the version in .bazelversion.
BAZELISK_VERSION=1.30.0
declare -A BAZELISK_SHA256=(
    [amd64]=a248ae2b2b62b5baebb748f6fba7203bff1a27e5d01b55399a8406744cb539e8
    [arm64]=b2bcc040c213b44586b121bda6d983c35df121b917c3a7a66fb16f2fa0df1a4f
)
add_program bazel "bazelisk $BAZELISK_VERSION" \
    "https://github.com/bazelbuild/bazelisk/releases/download/v$BAZELISK_VERSION/bazelisk-linux-$arch" \
    "${BAZELISK_SHA256[$arch]}"

# buildifier, which formats and lints Bazel files. `bazel run format` brings
# its own; this one is for editors, like VS Code's Bazel extension, which
# look for it on PATH.
BUILDIFIER_VERSION=10.1.0
declare -A BUILDIFIER_SHA256=(
    [amd64]=31b6a8aa1e5c746696788f428729701770ad91925873d8256cb885c60e12c77e
    [arm64]=38d2ed845f560b4a16ddee41de906508a95f8dc85b04e0851b0a71e3a70d3890
)
add_program buildifier "$BUILDIFIER_VERSION" \
    "https://github.com/bazelbuild/buildtools/releases/download/v$BUILDIFIER_VERSION/buildifier-linux-$arch" \
    "${BUILDIFIER_SHA256[$arch]}"

# Installs program $1, unless the file in /usr/local/bin already has the
# expected SHA-256 and this isn't --reinstall.
install_program() {
    local name=$1 dest=/usr/local/bin/$1 installed=false tmp
    local version=${program_version[$1]} url=${program_url[$1]}
    local sha256=${program_sha256[$1]}
    if [[ -f "$dest" ]] && echo "$sha256  $dest" | sha256sum --check --status; then
        installed=true
    fi
    if $installed && ! $reinstall; then
        echo "$name is already the pinned version ($version)$($simulate && echo ", would skip")."
        return
    fi
    if $simulate; then
        if $installed; then
            echo "Would reinstall $name ($version):"
        elif [[ -e "$dest" ]]; then
            echo "Would install $name ($version), replacing the one in /usr/local/bin:"
        else
            echo "Would install $name ($version):"
        fi
        echo "  Download: $url"
        echo "  Expected SHA-256 of the download: $sha256"
        return
    fi
    tmp=$(mktemp)
    curl -fsSL -o "$tmp" "$url"
    echo "$sha256  $tmp" | sha256sum --check --strict --quiet
    install -m 755 "$tmp" "$dest"
    rm "$tmp"
    echo "Installed $name ($version)."
}

section "manual packages installed in /usr/local/bin: ${programs[*]}"
for program in "${programs[@]}"; do
    install_program "$program"
done
