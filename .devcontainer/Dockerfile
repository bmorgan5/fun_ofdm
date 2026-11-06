# Build environment for fun_ofdm.
FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive

# What running, building and developing fun-ofdm need, from the same script
# as a host uses. "bootstrap" is tools/bootstrap/, a second build context that
# devcontainer.json adds.
COPY --from=bootstrap bootstrap.sh /tmp/bootstrap.sh
RUN /tmp/bootstrap.sh \
    && rm /tmp/bootstrap.sh \
    && rm -rf /var/lib/apt/lists/*

# Tools for the container only: bsdtar for `tar` (below), locales, sudo for
# installing packages in a running container, and an editor.
RUN apt update && apt install -y --no-install-recommends \
        libarchive-tools \
        locales \
        sudo \
        vim \
    && rm -rf /var/lib/apt/lists/*

# The en_US.UTF-8 locale, which VS Code's terminal sets LANG to. The base image
# only has C and C.UTF-8, and some programs, such as the perl that runs lcov's
# genhtml (`bazel run coverage`), warn about a locale that isn't installed.
RUN locale-gen en_US.UTF-8

# Use bsdtar as `tar`. GNU tar on this release extracts with the openat2
# syscall, which Rosetta (x86_64 emulation on Apple Silicon) doesn't
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

# A fixed output base, instead of Bazel's default (a hash of the workspace
# path), so the IntelliSense settings in devcontainer.json can name it. Bazel
# reads /etc/bazel.bazelrc on every run. It's still under ~/.cache/bazel, on
# the volume that keeps Bazel's cache across rebuilds.
RUN echo 'startup --output_base=/home/ubuntu/.cache/bazel/fun_ofdm' > /etc/bazel.bazelrc

# A link to Bazel's sysroot (not the system's) for the container's
# architecture, for IntelliSense in devcontainer.json. It dangles until the
# first build downloads the sysroot.
RUN mkdir -p /usr/local/share/fun_ofdm \
    && ln -s "/home/ubuntu/.cache/bazel/fun_ofdm/external/+sysroot+sysroot_linux_$(uname -m)/sysroot" \
        /usr/local/share/fun_ofdm/bazel_sysroot

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
