# fun-ofdm

This directory is `fun-ofdm`, which runs the PHY in [phy/](phy/README.md) with a
USRP or in software. It's also an example of how to
use the PHY: [transmitter.h](transmitter.h) and [receiver.h](receiver.h) are the
simplest way to send and receive frames over the air.

How to build and run it, and every option, are in the
[top-level README](../README.md). This page is the map: what each mode does,
what it sends, and where the code is.

## The modes

Each mode is a function in [main.cpp](main.cpp), and each one runs until it
has sent everything or Ctrl+C requests a stop, then prints a summary.

```
tx:   random_source → fragmenter → transmitter (frame_builder → usrp) → air
rx:   air → receiver (usrp → receiver_chain) → reassembler → summary
sim:  random_source → fragmenter → frame_builder → receiver_chain → reassembler
                                                                  → check against what was sent
```

- **`tx`** (`run_tx()`) sends random data over the air, one frame at a time,
  until it has sent `--packets` or `--bytes`, or until Ctrl+C.
- **`rx`** (`run_rx()`) receives frames until Ctrl+C. It counts the frames
  that were lost or received twice from their sequence numbers.
- **`sim`** (`run_sim()`) needs no USRP. It builds each frame's samples and
  passes them straight to the receiver chain, without a channel in between,
  then checks every byte it received against what it sent. It prints PASS and
  exits with status 0, or prints FAIL and exits with 1, so it works as an end
  to end test.

`tx` and `rx` ask for real time thread priority first ([realtime.h](realtime.h)),
which needs root, unless `--no-realtime` is given.

Ctrl+C is handled by a thread that waits for SIGINT and SIGTERM
(`stop_on_signals()` in main.cpp). The first one requests a stop on a
`std::stop_token` that every mode checks between frames, and the second exits
straight away.

## What's sent: the frame payload

Every frame's payload, which the PHY sends as its PSDU, is a 4-byte header and
then the data:

```
┌─────────────────┬──────────────────────────────┐
│ Sequence number │ Data                         │
│ 4 bytes         │ up to 1496 bytes             │
│ big-endian      │                              │
└─────────────────┴──────────────────────────────┘
◄───────────── at most 1500 bytes ──────────────►
```

- **Data:** random bytes, from `random_source` ([payload.h](payload.h)). Every
  frame is full except the last one.
- **Sequence number:** 0 for the first frame, and one more for each frame
  after it. The PHY's CRC-32 already drops corrupted frames, so this is only
  for telling which frames went missing or arrived twice
  (`reassembler`, in [framing.h](framing.h)).

This is fun-ofdm's own format, for testing the link. It isn't an 802.11 MAC
frame.

## The code

- [main.cpp](main.cpp): parses the command line and runs a mode.
- [options.h](options.h): the command line options, and turning them into
  `usrp_params`.
- [payload.h](payload.h): sources of data to send.
- [framing.h](framing.h): splits data into frame payloads (`fragmenter`), and
  checks the sequence numbers of received ones (`reassembler`).
- [stats.h](stats.h): counts packets and bytes, and prints the summary.
- [transmitter.h](transmitter.h): builds a frame and sends it with a USRP.
- [receiver.h](receiver.h): reads samples from a USRP and passes the frames it
  decodes to a callback.
- [realtime.h](realtime.h): asks for real time thread priority.

Most of this is the `:tools` library in [BUILD.bazel](BUILD.bazel), which
doesn't depend on UHD, so its tests run without a USRP. Only `main.cpp`,
`transmitter`, `receiver` and `realtime` are built straight into the binary.
Each `<name>_test.cpp` tests `<name>.h`.

## Known bugs

Each is flagged with `\bug` where it is:

- **Restarted transmitters:** `reassembler` takes a restarted transmitter's
  frames for duplicates, because their sequence numbers start again at 0. Its
  test is skipped.
- **Printing:** `rx` prints a line for every packet, and flushes it, on the
  thread that reads from the USRP.
- **Short reads:** after an overflow, `rx` processes the end of the previous
  batch of samples again (see [usrp/usrp.h](usrp/usrp.h)).
