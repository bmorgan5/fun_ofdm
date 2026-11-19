# Hardware tests

Hardware in the loop (HIL) tests require a USRP.

## Quick start

> [!WARNING]
> Never cable TX straight into RX: put at least 30 dB of attenuation between
> them, or the transmitter can damage the receiver. The tests refuse less.

Connect a USRP, then run:

```bash
bazel run install
uv run pytest hil --topology loopback --atten-db 40 --sudo  # Already set up
uv run pytest hil --interactive --sudo  # Pick a topology, and walk through it
```

`uv run pytest hil --help` lists the options, under "hardware tests".

A missing precondition, like no USRP, is an **error**, and a missed exit
criterion is a **failure**. Each test file writes `result.json` and `result.md`
to `out/hil/<test>/`.

| Test | What it measures |
|---|---|
| [test_link_smoke.py](test_link_smoke.py) | 10,000 frames at BPSK 1/2 and 1,000 at QAM16 1/2 over the link: losses, duplicates, overflows, false detections and sample-arrival jitter |

## Topologies

A topology is how the radios are connected. The tests are the same on each:
each topology provides the link they send frames over (`LINKS` in
[conftest.py](conftest.py)). [hardware.py](../tools/hil/hardware.py) has their
diagrams and steps.

- **`loopback`:** one B210, from RF A's TX/RX port → 30 to 40 dB attenuator →
  RF B's TX/RX port.
- **`cabled`:** two B210s. **TODO:** its link, which needs `rx` to check the
  bytes it receives.
- **TODO:** `over-the-air`, two B210s with antennas, and `wifi`, a B210 with a
  Wi-Fi access point and a laptop.

## Where to run them

Record results on a Raspberry Pi 5 or a native Linux machine, with the B210 on
a USB 3 port and its external supply. They run in the dev container too
([README.md](../README.md#development)), but over USB/IP a B210 has stopped
streaming in the middle of a run, and the timings are slower.
