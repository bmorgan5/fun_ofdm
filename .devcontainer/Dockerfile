# Build environment for fun_ofdm.
FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive

# Development tools.
RUN apt update && apt install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        libarchive-tools \
        locales \
        python3 \
        sudo \
        vim \
    && rm -rf /var/lib/apt/lists/*

# Runtime libraries for running the binaries built here. Builds link against
# the sysroot (see MODULE.bazel), but libuhd is linked dynamically, so the
# binaries load it from the system at run time. Keep the version in step with
# the libuhd in the sysroot.
RUN apt update && apt install -y --no-install-recommends \
        libuhd4.9.0 \
    && rm -rf /var/lib/apt/lists/*

# The en_US.UTF-8 locale, which VS Code's terminal sets LANG to. The base image
# only has C and C.UTF-8, and some programs, such as the perl that runs lcov's
# genhtml (`bazel run coverage`), warn about a locale that isn't installed.
RUN locale-gen en_US.UTF-8

# Use bsdtar as `tar`. GNU tar on this release extracts with the openat2
# syscall, which Rosetta (x86-64 emulation on Apple Silicon) doesn't
# implement, so every extraction fails with "Function not implemented".
# That breaks VS Code, which unpacks its server with `tar` whenever VS Code
# updates. /usr/local/bin comes before /usr/bin on PATH.
RUN ln -s /usr/bin/bsdtar /usr/local/bin/tar

# GitHub CLI, from GitHub's apt repo (Ubuntu's package lags behind)
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        -o /usr/share/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt update && apt install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# Authenticate git to GitHub through the GitHub CLI, using HTTPS even when a
# remote is an SSH URL (git@github.com:...), so no SSH keys are needed.
# --system so VS Code's copy of the host ~/.gitconfig can't override it.
RUN git config --system credential.https://github.com.helper '!gh auth git-credential' \
    && git config --system url."https://github.com/".insteadOf "git@github.com:"

COPY git-editor /usr/local/bin/git-editor
RUN chmod 755 /usr/local/bin/git-editor
ENV GIT_EDITOR=git-editor

# Bazel, through bazelisk, which runs the version in .bazelversion.
ARG BAZELISK_VERSION=1.30.0
ARG BAZELISK_SHA256_AMD64=a248ae2b2b62b5baebb748f6fba7203bff1a27e5d01b55399a8406744cb539e8
ARG BAZELISK_SHA256_ARM64=b2bcc040c213b44586b121bda6d983c35df121b917c3a7a66fb16f2fa0df1a4f
RUN arch=$(dpkg --print-architecture) \
    && case $arch in \
        amd64) sha256=$BAZELISK_SHA256_AMD64 ;; \
        arm64) sha256=$BAZELISK_SHA256_ARM64 ;; \
    esac \
    && curl -fsSL -o /usr/local/bin/bazel \
        "https://github.com/bazelbuild/bazelisk/releases/download/v${BAZELISK_VERSION}/bazelisk-linux-${arch}" \
    && echo "${sha256}  /usr/local/bin/bazel" | sha256sum --check --strict \
    && chmod 755 /usr/local/bin/bazel

# A fixed output base, instead of Bazel's default (a hash of the workspace
# path), so the IntelliSense settings in devcontainer.json can name it. Bazel
# reads /etc/bazel.bazelrc on every run. It's still under ~/.cache/bazel, on
# the volume that keeps Bazel's cache across rebuilds.
RUN echo 'startup --output_base=/home/ubuntu/.cache/bazel/fun_ofdm' > /etc/bazel.bazelrc

# buildifier, which formats and lints Bazel files (used by the VS Code Bazel
# extension, which looks for it on PATH).
ARG BUILDIFIER_VERSION=10.1.0
ARG BUILDIFIER_SHA256_AMD64=31b6a8aa1e5c746696788f428729701770ad91925873d8256cb885c60e12c77e
ARG BUILDIFIER_SHA256_ARM64=38d2ed845f560b4a16ddee41de906508a95f8dc85b04e0851b0a71e3a70d3890
RUN arch=$(dpkg --print-architecture) \
    && case $arch in \
        amd64) sha256=$BUILDIFIER_SHA256_AMD64 ;; \
        arm64) sha256=$BUILDIFIER_SHA256_ARM64 ;; \
    esac \
    && curl -fsSL -o /usr/local/bin/buildifier \
        "https://github.com/bazelbuild/buildtools/releases/download/v${BUILDIFIER_VERSION}/buildifier-linux-${arch}" \
    && echo "${sha256}  /usr/local/bin/buildifier" | sha256sum --check --strict \
    && chmod 755 /usr/local/bin/buildifier

# Run as the base image's built-in non-root "ubuntu" user. On Linux hosts
# VS Code remaps its UID/GID to match the host user. The user gets
# passwordless sudo, for installing packages in a running container.
RUN echo "ubuntu ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/ubuntu \
    && chmod 0440 /etc/sudoers.d/ubuntu \
    && mkdir -p /home/ubuntu/.claude /home/ubuntu/.config/gh /home/ubuntu/.cache/bazel \
    && chown -R ubuntu:ubuntu /home/ubuntu/.claude /home/ubuntu/.config /home/ubuntu/.cache
USER ubuntu

# Claude Code CLI (native installer, installs to ~/.local/bin). The installer
# leaves config behind in ~/.claude and ~/.claude.json; remove it so the
# ~/.claude volume starts empty instead of being seeded with stale backups.
RUN curl -fsSL https://claude.ai/install.sh | bash \
    && rm -rf ~/.claude/* ~/.claude.json
    ENV PATH=/home/ubuntu/.local/bin:$PATH
