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
ARCHIVE=fun-ofdm-v$RELEASE-linux-x86_64
gh release download "v$RELEASE" --repo bmorgan5/fun_ofdm --pattern "$ARCHIVE.tar.gz"
gh attestation verify "$ARCHIVE.tar.gz" --repo bmorgan5/fun_ofdm
tar -xzf "$ARCHIVE.tar.gz"
"$ARCHIVE/bin/fun-ofdm" sim
```

`sim` needs no USRP. It sends 100 packets of random data through the transmit
and receive chains, and checks every byte:

```text
Simulating at 3/4 QAM16
Received 100 packets (149600 bytes) in 0.047 s, 25.3 Mbit/s
Lost 0 packets (0.0%), 0 duplicates
PASS: all 100 packets received intact
```

It exits with status 0 on PASS and 1 on FAIL.

## Requirements

`fun-ofdm` runs on x86-64 Ubuntu 26.04 with Ettus's UHD library:

```bash
sudo apt install libuhd4.9.0
```

`tx` and `rx` also need a USRP N210, each with its own gigabit Ethernet port.
The code was tested with XCVR2450 daughterboards.

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

## Development

To build from source, open the repo in its dev container (VS Code's "Dev
Containers: Reopen in Container"), or install
[bazelisk](https://github.com/bazelbuild/bazelisk) as `bazel`. Bazel downloads
the compiler and the libraries itself. Common build commands:

```bash
bazel build fun-ofdm       # The binary, in bazel-bin/src/fun-ofdm
bazel test //...           # The tests
bazel run fun-ofdm -- sim  # Builds the binary and runs sim
bazel run coverage         # Coverage, with an HTML report in out/coverage/
bazel run format           # Formats the C++ and Bazel files
bazel run install          # Copies the binary to out/<platform>/bin/
bazel build //docs         # The API docs, in bazel-bin/docs/html/
```

Every build is a cross compile against an Ubuntu 26.04 sysroot, built from a
dated snapshot of the Ubuntu archive (`UBUNTU_SNAPSHOT` in `MODULE.bazel`),
so the build never uses the host's compiler or libraries. Only x86-64 builds
for now, because the code uses SSE intrinsics.

Outside the dev container, running the binary needs the
[requirements](#requirements) above, though the build doesn't. Also enable the
pre-commit hook, which checks formatting, and have `git blame` skip the
reformat commit:

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
