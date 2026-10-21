# Build environment for fun_ofdm.
FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        doxygen \
        git \
        libboost-all-dev \
        libfftw3-dev \
        libuhd-dev \
        pkg-config \
        vim \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI, from GitHub's apt repo (Ubuntu's package lags behind)
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        -o /usr/share/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=amd64 signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# Authenticate git to GitHub through the GitHub CLI, using HTTPS even when a
# remote is an SSH URL (git@github.com:...), so no SSH keys are needed.
# --system so VS Code's copy of the host ~/.gitconfig can't override it.
RUN git config --system credential.https://github.com.helper '!gh auth git-credential' \
    && git config --system url."https://github.com/".insteadOf "git@github.com:"

COPY git-editor /usr/local/bin/git-editor
RUN chmod 755 /usr/local/bin/git-editor
ENV GIT_EDITOR=git-editor

# Run as the base image's built-in non-root "ubuntu" user. On Linux hosts
# VS Code remaps its UID/GID to match the host user.
RUN mkdir -p /home/ubuntu/.claude /home/ubuntu/.config/gh \
    && chown -R ubuntu:ubuntu /home/ubuntu/.claude /home/ubuntu/.config
USER ubuntu

# Claude Code CLI (native installer, installs to ~/.local/bin). The installer
# leaves config behind in ~/.claude and ~/.claude.json; remove it so the
# ~/.claude volume starts empty instead of being seeded with stale backups.
RUN curl -fsSL https://claude.ai/install.sh | bash \
    && rm -rf ~/.claude/* ~/.claude.json
    ENV PATH=/home/ubuntu/.local/bin:$PATH
