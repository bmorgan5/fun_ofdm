# The fun-ofdm binary

This directory holds the `fun-ofdm` binary, which drives the PHY in
[phy/](phy/README.md) with a USRP (`tx` and `rx`) or entirely in software
(`sim`). This page covers how each mode moves data, what each frame carries,
and which file does what.

## The modes

Each mode is a function in [main.cpp](main.cpp) that runs until it has sent
everything or is stopped, then prints a summary. With `--json`, `run_mode()`
also writes the summary to a file as JSON: the version, the mode, its options,
the seed and the results ([summary.h](summary.h) lists the members).

```text
tx:   random_source → fragmenter → transmitter (frame_builder → usrp) → air
rx:   air → receiver (usrp → receiver_chain) → reassembler → summary
sim:  random_source → fragmenter → frame_builder → channel → receiver_chain → reassembler
                                                                            → check against what was sent
```

- **`tx`** (`run_tx()`) sends random data over the air, one frame at a time.
- **`rx`** (`run_rx()`) receives frames, and counts the ones lost or received
  twice from their sequence numbers.
- **`sim`** (`run_sim()`) passes each frame's samples through a simulated
  channel ([channel.h](channel.h)) to the receiver chain, and checks every byte
  against what it sent. The channel is perfect unless options add noise, a
  frequency offset, a delay or multipath.

`tx` and `rx` first ask for real time thread priority
([realtime.h](realtime.h)). A thread waits for SIGINT and SIGTERM
(`stop_on_signals()`): the first one requests a stop on a `std::stop_token`
that each mode checks between frames, and the second exits immediately.

## The frame payload

Each frame's payload, which the PHY sends as its PSDU, is a sequence number
followed by the data:

```text
┌─────────────────┬──────────────────────────────┐
│ Sequence number │ Data                         │
│ 4 bytes         │ up to 1496 bytes             │
│ big-endian      │                              │
└─────────────────┴──────────────────────────────┘
◄───────────── at most 1500 bytes ──────────────►
```

The data is random bytes from `random_source` ([payload.h](payload.h)), and
every frame is full except the last. The bytes depend only on the seed, so
`--seed` sends the same data again. The sequence number starts at 0 and goes
up by one per frame. The PHY's CRC-32 already drops corrupted frames, so the
sequence number only tells `reassembler` ([framing.h](framing.h)) which frames
went missing or arrived twice. This is fun-ofdm's own format for testing the
link, not an 802.11 MAC frame.

## The code

Start with [main.cpp](main.cpp): each mode's function there shows how the
pieces in the diagrams fit together. All but `main.cpp`, `transmitter`,
`receiver` and `realtime` make up the `:tools` library in
[BUILD.bazel](BUILD.bazel). It doesn't depend on UHD, so its tests run without
a USRP. Each `<name>_test.cpp` tests `<name>.h`.

## Known bugs

Each is flagged with `\bug` where it is:

- **Restarted transmitters:** `reassembler` takes a restarted transmitter's
  frames for duplicates, because their sequence numbers start again at 0. Its
  test is skipped.
- **Printing:** `rx` prints a line for every packet, and flushes it, on the
  thread that reads from the USRP.
- **Short reads:** after an overflow, `rx` processes the end of the previous
  batch of samples again (see [usrp/usrp.h](usrp/usrp.h)).
