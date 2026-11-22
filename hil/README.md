# Hardware tests

Hardware in the loop (HIL) tests that require a USRP .

## Setup

Set up one B210 to send from a TX/RX port to another port, through a 40 dB
attenuator. By default, the tests send from RF B's TX/RX port to RF B's RX2
port:

```text
  ┌────────────────────── B210 ──────────────────────┐
  │   RF A: TX/RX   RX2            RF B: TX/RX   RX2  │
  └────────────────────────────────────────┬──────┬───┘
                                           │      │
                                           └──┬───┘
                                              │
                                      40 dB attenuator
```

> [!WARNING]
> Never cable TX straight into RX: without at least 30 dB of attenuation, the
> transmitter can damage the receiver.

The default gains, `--tx-gain 80` and `--rx-gain 60`, are set for 40 dB. With
another attenuator, adjust them to match. Other ports work too: name them with
`--tx-port` and `--rx-port`, like `--tx-port A:TX/RX --rx-port B:TX/RX`.

Plug the B210 into a USB 3 port. UHD needs root, or UHD's udev rules, to open
it. Then run:

```bash
bazel run install
uv run pytest hil
```

## Suites

- [test_link_smoke.py](test_link_smoke.py): sends 1,000 frames at each of a
  low, a middle and a high rate (BPSK 1/2, QAM16 1/2 and QAM64 3/4), and
  checks that 95% of them arrive intact.

**TODO:** tests with two B210s, cabled to each other or over the air with
antennas.
