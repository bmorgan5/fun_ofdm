# Hardware sweeps

Measurements of fun-ofdm on a B210, set up as for the
[hardware tests](../../hil/README.md): one B210, cabled from a TX/RX port to
another port through an attenuator. [hardware.py](hardware.py) has what the
sweeps and the hardware tests share, like finding the B210 and running
fun-ofdm with a time limit.

```bash
bazel run install
uv run tools/hardware/hardware_sweep.py realtime --out realtime/   # About 8 minutes
uv run tools/per/sweep.py --snr-min 14 --snr-max 24 --snr-step 0.5 --frames 1000 --out per/
uv run tools/hardware/hardware_sweep.py per --sim per/ --out per-hardware/   # About 15 minutes
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

After the sweep, it runs each configuration once more with `--trace`, for 1
second of frames, or for idle about as long after opening the USRP, and saves
the trace, gzipped, with the run's summary, to `traces/`: for
[charts.py](../scorecard/charts.py)'s timelines, or a closer look in
[ui.perfetto.dev](https://ui.perfetto.dev).

The headline, for each: the highest bandwidth with no overflows in any run,
at it and every lower bandwidth. `--out` writes every step, and the tables, as
`result.json` and `result.md` ([the result contract](../results/result.py)).
`--help` lists the options.

## `per`: how close to the simulated curves?

At 5 MHz, which the receiver keeps up with, it steps the TX gain 1 dB at a
time across each rate's waterfall, from about 100% PER to about 0%, sending
500 frames of 1000-byte payloads a step, as the [simulated sweep](../per/README.md)
does. A first step at TX gain 75 finds roughly where each rate's simulated 10%
PER point is, and each rate starts 5 dB either side of it.

Each step records the PER and the receiver's SNR estimate. SNR follows TX gain
one for one, so it fits SNR = TX gain + offset to the steps where at least half
the frames reached the estimate and less than half were lost, and uses the fit
at every step: below that, only the louder frames get through, so the estimate
reads high. The fit's slope is recorded too, and flagged if it isn't about 1.
A step whose run failed is recorded, but left out of the curve.

The headline, for each rate: its implementation loss, its SNR at 10% PER minus
the simulation's, from the `--sim` result.
