# v2.0.0 Modernize

## Summary

This release replaces the unmaintained CMake build with a hermetic Bazel build,
unit tests and CI on every pull request. The DSP is unchanged from v1.0.0, so
this release is the baseline that future releases measure against.

## What's new

- `fun-ofdm` is one binary with `tx`, `rx` and `sim` modes. `sim` runs the
  transmit and receive chains in software, so it doesn't need any USRPs.
- The frequency/channel, bandwidth, gain, device, TX amplitude and PHY rate
  are CLI options instead of constants in the source. A gain outside the
  device's range is an error instead of being silently clipped.
- Ctrl+C stops every mode cleanly, and each mode prints a summary of what it
  sent or received.
- Every payload starts with a sequence number, so `rx` counts lost and
  duplicate frames, and `sim` checks every byte it receives.
- The build is hermetic: Bazel downloads clang and an Ubuntu 26.04 sysroot, and
  our own code builds with `-Wall -Wextra -Werror`.
- CI runs the tests in optimized, ASan, UBSan and TSan builds on every pull
  request, along with coverage and a format check.
- Each release has a prebuilt x86-64 binary, with `SHA256SUMS` and a build
  provenance attestation. The README's
  [Quick start](https://github.com/bmorgan5/fun_ofdm/blob/v2.0.0/README.md#quick-start)
  shows how to download, verify and run it.
- The project is now licensed under the GPL-3.0-or-later.

## Results

| | v2.0.0 |
|---|---|
| Unit tests | 554 in under 1 s: 351 pass, 193 are skipped for known bugs, and 10 for differences from 802.11a |
| Line coverage | 97.5% of the PHY, and 97.4% of `src/` |
| Loopback | `sim` decodes 1000 of 1000 frames at each of the 11 rates |
| Build time | 33 s clean and 0.9 s with no changes, on a 16-thread i7-10700K, after a one-time download of about 14 GB |

The receiver chain only decodes batches of more than 160 samples, because of
the short batch bugs below. The loopback tests for smaller batches are skipped
until those bugs are fixed.

## Bug fixes

- Destroying a `receiver_chain` called `std::terminate`, and `receiver` started
  a thread that it never joined. Both now shut down cleanly, and TSan checks it.
- An invalid SIGNAL rate field was undefined behaviour. It now aborts.

## Known bugs

Each bug is flagged with `\bug` in the named header, and has a skipped test.

- The receiver chain only decodes reliably in batches of more than 160 samples
  (`frame_detector.h`, `timing_sync.h`, `receiver_chain.h`).
- The frequency offset estimate is always 0 (`timing_sync.h`).
- The tail bits are scrambled, so the encoder doesn't always end in state 0 as
  the Viterbi decoder assumes. Frames still get through (`ppdu.h`).
- `fft` never frees its FFTW buffers and plans (`fft.h`).
- The USRP wrapper processes stale samples after an overflow, misses
  underflows, ignores `--tx-amp` in `send_burst()`, and streams RX samples while
  only transmitting (`usrp.h`).
- `rx` takes the frames of a restarted transmitter for duplicates
  (`framing.h`).
- `rx` prints and flushes a line for every packet on the thread that reads from
  the USRP (`main.cpp`).

The PHY also
[differs from 802.11a](https://github.com/bmorgan5/fun_ofdm/blob/v2.0.0/src/phy/README.md#differences-from-80211a),
so for now it only talks to other copies of fun-ofdm.

## Changelog

Each issue was closed by one pull request:

- Build with Bazel: issue #6, pull request #18
- Automatic formatting: issue #7, pull request #19
- Formatting for developers: issue #8, pull request #20
- Document the PHY: issue #9, pull request #21
- Test the transmit side: issue #10, pull request #22
- CI: issue #11, pull request #23
- Sanitizers and warnings: issue #12, pull request #24
- Coverage: issue #13, pull request #25
- Test the receiver chain: issue #14, pull request #26
- Tool options and clean shutdown: issue #15, pull request #27
- Random payloads: issue #16, pull request #28
- Release v2.0.0: issue #17, pull request #29

[Every commit between v1.0.0...v2.0.0](https://github.com/bmorgan5/fun_ofdm/compare/v1.0.0...v2.0.0)
