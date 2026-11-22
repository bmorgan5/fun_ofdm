# Hardware sweeps

Measurements of fun-ofdm on a B210, set up as for the
[hardware tests](../../hil/README.md): one B210, cabled from a TX/RX port to
another port through an attenuator. [hardware.py](hardware.py) has what the
sweeps and the hardware tests share, like finding the B210 and running
fun-ofdm with a time limit.

```bash
bazel run install
uv run tools/hardware/hardware_sweep.py realtime --out realtime/   # About 8 minutes
```

## `realtime`: can it keep up?

At each of 802.11a's bandwidths, 5, 10 and 20 MHz, which are also the sample
rates, it sends frames over the cable at BPSK 1/2, the lightest rate to decode,
and at QAM64 3/4, the heaviest, for 5 seconds, and runs `rx` with nothing sent,
the receiver's floor, for 10 seconds more than that, counting the time it takes
to open the USRP. `rx` has no port options, so it receives on its default port,
which only hears noise too. It runs each step 3 times. Each step records the
USRP's overflows, the frames lost, the latency, each block's processing time,
and each CPU's use, which it samples from `/proc/stat`.

The headline, for each: the highest bandwidth with no overflows in any run,
at it and every lower bandwidth. `--out` writes every step, and the tables, as
`result.json` and `result.md` ([the result contract](../results/result.py)).
`--help` lists the options.
