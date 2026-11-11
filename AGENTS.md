# AGENTS.md

Instructions for coding agents working on fun_ofdm.

Start with [README.md](README.md).

## Commands

Run everything from the repo root. Bazel downloads the compiler (clang, from
toolchains_llvm) and a sysroot, so the build never uses the host's compiler or
libraries.

| Task | Command |
|---|---|
| Install the tools (as the dev container has them) on an Ubuntu 26.04 host | `sudo tools/bootstrap/bootstrap.sh` |
| Build everything | `bazel build //...` |
| Build the binary | `bazel build fun-ofdm` |
| Smoke test (sends and decodes 100 packets) | `bazel run fun-ofdm -- sim` |
| Benchmarks (about 1.5 min; see `src/bench/README.md`) | `bazel run fun-ofdm-bench` |
| Build without the metrics, to measure their cost | `--config=no_metrics` |
| Coverage: prints the totals and writes an HTML report to `out/coverage/` | `bazel run coverage` |
| Coverage compared with an earlier `out/coverage/` (a copy of it) | `bazel run coverage -- --baseline <dir>` |
| Build the API docs (Doxygen) | `bazel build //docs` |
| Copy the binaries to `out/<platform>/bin/` | `bazel run install` |
| Format the C++ and Starlark files | `bazel run format` |
| Check formatting, changing nothing | `bazel run format.check` |
| Install Python and the Python tools' packages | `uv sync` |
| Test the Python tools, in `tools/` | `uv run pytest` |
| Test `fun-ofdm` from Python, in `tests/`, after `bazel run install` (as CI) | `uv run pytest tests` |
| PER vs SNR at every rate (see `tools/per/README.md`) | `uv run tools/per/sweep.py` |
| Format and lint the Python tools | `uv run ruff format` and `uv run ruff check` |
| Check the Python tools' types | `uv run ty check` |
| Add a Python package | `uv add <package>` (`--dev` if only for development) |

- Format before proposing a commit: `bazel run format.check` must pass. Run
  from the repo root, `bazel run format` formats every file;
  `bazel run format -- <files>` formats only those. For Python files,
  `uv run ruff format --check`, `uv run ruff check` and `uv run ty check`
  must pass too.
- Builds are optimized by default. Use `-c dbg` for a debug build.
- Builds target the host's architecture by default. `--config=aarch64` and
  `--config=x86_64` cross compile from either host, and `bazel test` and
  `bazel run` then run the other architecture's binaries under qemu.
- `rx` and `tx` need a USRP. Use `sim` to check a change end to end.
- If a change touches `MODULE.bazel`, commit the updated `MODULE.bazel.lock`
  with it. Likewise `pyproject.toml` and `uv.lock`.

## Layout

- `src/`: `fun-ofdm` (`main.cpp`). The PHY blocks are in `src/phy/`, the
  USRP wrapper is in `src/usrp/`, `fun-ofdm-bench` is in `src/bench/`, and
  the receiver's histograms and traces are in `src/metrics/`.
- `third_party/`: Bazel targets for FFTW and UHD, from the sysroot, and BUILD
  files for HdrHistogram_c and the Perfetto SDK.
- `bazel/sysroot/`: builds the sysroot from Ubuntu packages.
- `bazel/sanitizers/`: the sanitizer configs' runtime options, for the tests.
- `platforms/`: the target platforms.
- `docs/`: the Doxygen build.
- `tools/`: `bootstrap/`, the tools that Bazel runs (`coverage/`, `format/`,
  `qemu/`), the Perfetto config for system traces (`trace/`), and the Python
  tools (`pyproject.toml`), like the PER sweep (`per/`), with their own tests.
- `tests/`: Python tests of `fun-ofdm` itself, which run the binary, like the
  PER guardrails.

## Conventions

- **Scope.** The project implements the OFDM PHYs: 802.11a, and later 802.11g.
- **Dependencies.** Keep them minimal. Add one only when the alternative is
  writing a lot of code. Build dependencies go through Bazel
  (`MODULE.bazel`), and the Python tools' through uv (`pyproject.toml`). What
  the host needs, like apt packages or Bazel itself, goes in
  `tools/bootstrap/bootstrap.sh`, which the dev container runs too.
- **Tests.** No randomness. Test data comes from a PRBS sequence, known
  vectors, or every possible input.
- **Bugs before fixes.** Don't change behaviour without a test. For a known bug,
  write the test first and skip it, with the reason, then fix it and un-skip it.
- **Flag bugs and differences from the standard** in the code's Doxygen docs,
  and don't describe them as normal behaviour:
  - `\bug` for code that's wrong by its own design.
  - An "Unlike 802.11a" list for deliberate differences from the standard.
  - `\todo` for a planned change, plus a **TODO** in the directory's README.
- **Integer types**, following Google's style guide:
  - `std::uint8_t` for bytes and bits, not `unsigned char`.
  - Fixed-width types (`std::uint16_t`, `std::uint32_t`, ...) where the size
    matters: bit fields like the SIGNAL field, CRCs, shift registers, SIMD
    lanes.
  - `std::size_t` for sizes and indices. A loop counter that's used as an
    index is an index, so it's a `std::size_t` too. A position that can be
    negative, like an offset before it's checked, is a `std::ptrdiff_t`.
  - `int` for other small values, including loop counters that don't index
    anything. Don't use an unsigned type just to say a value can't be
    negative.
  - No C-style casts: use `std::size_t{1}`-style braces where the value
    always fits, and `static_cast` otherwise. Better still, pick types that
    don't need a cast.
- **Moving files.** Use `git mv`, so history follows the file.
- **Docs.** Markdown follows Google's
  [Markdown style guide](https://google.github.io/styleguide/docguide/style.html):
  80 columns, except for links, tables, headings and code blocks. Docs follow
  its [minimum viable documentation](https://google.github.io/styleguide/docguide/best_practices.html#minimum-viable-documentation)
  principle: keep them short and current, and link to the code, `--help` or
  another doc instead of repeating it.

## Workflow

- Work is tracked in GitHub issues, grouped by milestone (one milestone per
  release). Each issue is one pull request.
- Branches are named `<user>/<issue title>`, lowercased with dashes, for
  example `bmorgan5/1.1-build-with-bazel`.
- The PR description says `Closes #N`. PRs are merged by rebase, so every
  commit lands on `main` as it is.
- The PR description summarizes the change as a whole, and how it was tested.
  Don't list the commits: a workflow adds their messages to the description
  on every push.
- CI (`.github/workflows/ci.yml`) runs `bazel test //...`, the PER tests
  (`uv run pytest tests`), `bazel run //:format.check` and the Python tools'
  checks on every PR, so they must pass locally first.
- If a PR closes an issue, the issue is the unit of work: only the issue goes
  in the GitHub project and the milestone, and the PR shows up on it as a
  linked pull request. A PR with no issue goes in the project and the milestone
  itself.
- Make the change, then stop so it can be reviewed. Don't stage, commit or push
  it. Instead, give a commit message for it, following the rules below.
- A milestone's last PR makes its release: it bumps the version, writes
  `docs/releases/vX.Y.Z.md`, and sets `RELEASE` in the README's Quick start.
  After it merges, pushing the `vX.Y.Z` tag makes a draft release
  (`.github/workflows/release.yml`), which is published by hand.

## Commits

- One logical change per commit. It must build on its own.
- Subject: `area: Do the thing`, in the imperative, at most about 72
  characters. The area is a directory, block or tool, like `build`,
  `receiver_chain` or `docs`.
- Body: why the change is needed and what it does, wrapped at 72 columns.
- End the message with a `Co-Authored-By:` line naming the agent and model
  that made the change, for example:

  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```
