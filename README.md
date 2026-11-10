# fun-ofdm

[![CI](https://github.com/bmorgan5/fun_ofdm/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/bmorgan5/fun_ofdm/actions/workflows/ci.yml)

fun-ofdm is a real-time 802.11a
[OFDM physical layer](src/phy/README.md) in C++ for
[Ettus](https://www.ettus.com/) USRPs. It was originally written in 2014 at the
University of Washington's Fundamentals of Networking Lab (FUNLAB) for wireless
communications research. At the time, it could only handle 5 MHz channels
(5 megasamples per second or 20 MB/s of 32-bit samples over the Ethernet link
to the USRP). It's now being upgraded to 802.11a's full 20 MHz channels
(20 MS/s or 80 MB/s) for further PHY and MAC research.

## Quick start

On a machine that meets the [requirements](#requirements), download, verify,
and run the latest release from
[releases](https://github.com/bmorgan5/fun_ofdm/releases):

```bash
RELEASE=2.0.0
ARCHIVE=fun-ofdm-v$RELEASE-linux-$(uname -m)
gh release download "v$RELEASE" --repo bmorgan5/fun_ofdm --pattern "$ARCHIVE.tar.gz"
gh attestation verify "$ARCHIVE.tar.gz" --repo bmorgan5/fun_ofdm
tar -xzf "$ARCHIVE.tar.gz"
sudo "$ARCHIVE/bootstrap.sh" --runtime
"$ARCHIVE/bin/fun-ofdm" sim
```

`sim` needs no USRP. It sends 100 packets of random data through the transmit
and receive chains, and checks every byte:

```text
Simulating at 3/4 QAM16 with seed 6409142305361286791
Received 100 packets (149600 bytes) in 0.047 s, 25.3 Mbit/s
Lost 0 packets (0.0%), 0 duplicates
PASS: all 100 packets received intact
```

It exits with status 0 on PASS and 1 on FAIL.

By default the channel between them is perfect. `--snr`, `--cfo`,
`--timing-offset` and `--delay-spread` add noise, a carrier frequency offset, a
delay and multipath fading ([channel.h](src/channel.h)), for example
`fun-ofdm sim --snr 20 --cfo 2.5kHz`.

## Requirements

- An aarch64 or x86_64 machine running Ubuntu 26.04.
- Ettus's UHD library. See [bootstrap.sh](tools/bootstrap/bootstrap.sh), or
  run `bootstrap.sh [--runtime] --simulate`, for the full list of
  dependencies.
- The `fun-ofdm tx` and `fun-ofdm rx` each need a USRP N210 with its own gigabit
  Ethernet port. The code was tested with XCVR2450 daughterboards.

## Usage

For now, fun-ofdm only talks to other copies of itself, not to standard Wi-Fi
devices. Start the receiver, then the transmitter on a second USRP:

```bash
sudo fun-ofdm rx
sudo fun-ofdm tx --packets 1000
```

`rx` runs until Ctrl+C, then prints how many packets it received, lost and
received twice. Receiving more than 90% of the packets means the setup works.
On a quiet channel, `rx` also prints an occasional `Invalid CRC` line: a false
detection, which the CRC check dropped.

`fun-ofdm <mode> --help` lists the options: the frequency or 802.11 channel,
bandwidth, gain, PHY rate and how much data to send. For example:

```bash
sudo fun-ofdm tx --channel 36 --bandwidth 20 --gain 25 --phy-rate qam16-1/2
```

`--no-realtime` runs without real time priority, and so without root.

`tx` and `sim` print the seed of their random data: `--seed` repeats a run.
`--json FILE` also writes the run summary to `FILE` as JSON, for scripts.
Without `FILE` it goes to stdout, and the rest of the output to stderr:
`fun-ofdm sim --json | jq .passed`.

## Development

To build from source, open the repo in its dev container (VS Code's "Dev
Containers: Reopen in Container"), or run `sudo tools/bootstrap/bootstrap.sh`,
which installs the same tools as the dev container, including
[bazelisk](https://github.com/bazelbuild/bazelisk) as `bazel`. Bazel downloads
the compiler and the libraries itself. Common build commands:

```bash
bazel build fun-ofdm       # The binary, in bazel-bin/src/fun-ofdm
bazel test //...           # The tests
bazel run fun-ofdm -- sim  # Builds the binary and runs sim
bazel run coverage         # Coverage, with an HTML report in out/coverage/
bazel run format           # Formats the C++ and Bazel files
bazel run install          # Copies the binary to out/<platform>/bin/
bazel build //docs         # The API docs, in bazel-bin/docs/html/ (x86_64 only)
```

Every build is a cross compile against an Ubuntu 26.04 sysroot, built from a
dated snapshot of the Ubuntu archive (`UBUNTU_SNAPSHOT` in `MODULE.bazel`),
so the build never uses the host's compiler or libraries. Builds target the
host's architecture by default; `--config=aarch64` or `--config=x86_64` cross
compiles from either. With them, `bazel test` and `bazel run` run the other
architecture's binaries under qemu. The sanitizer configs only work in native
builds.

Outside the dev container, also enable the pre-commit hook, which checks
formatting, and have `git blame` skip the reformat commit:

```bash
git config core.hooksPath tools/githooks
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

## Troubleshooting

- Does `uhd_find_devices` find the USRP?
- Did you run `fun-ofdm` as root, or with `--no-realtime`?
- Do the transmitter and receiver use the same frequency and bandwidth?
- Have you tried other `--gain` and `--tx-amp` settings?
- UHD printing `O` or `D` means overflows or dropped packets: check the
  Ethernet card (see Ettus's
  [host tuning notes](https://kb.ettus.com/USRP_Host_Performance_Tuning_Tips_and_Tricks))
  and the CPU load.

## Contact

Open a [GitHub issue](https://github.com/bmorgan5/fun_ofdm/issues), with your
OS and version, USRP and daughterboard, the command you ran, and what happened.

## License

[GPL-3.0-or-later](LICENSE)
