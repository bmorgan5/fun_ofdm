# PER vs SNR

How much SNR the receiver needs to decode each rate. For each rate, the sweep
measures the packet error rate (PER) of 1000-byte payloads through an AWGN
channel at a range of SNRs, and finds the SNR at which the PER falls to 10%.
This guards against regressions in the receiver where more SNR is required to
decode.

## The sweep

```bash
bazel run install
uv run tools/per/sweep.py --out per.json   # Every rate, 0-30 dB, about a minute
```

[sweep.py](sweep.py) runs `fun-ofdm sim` at each point: `--frames` frames
(500 by default) of 1000-byte payloads at one SNR, with the noise from
[src/channel.h](../../src/channel.h). It prints each rate's SNR at 10% PER, and
`--out` writes every point. `--help` lists the options, like the SNRs and
rates to run. It runs the binary that `bazel run install` copies to
`out/linux_<arch>/bin/`, for this machine's architecture, unless `--fun-ofdm`
names another.

```text
PER
100% ┤━━━━━━━━━━━━━━━╮
     │                ╲          Each point: frames lost / frames sent,
     │                 ╲         with a 95% confidence interval
 10% ┤- - - - - - - - - ●- - -   ◄── the SNR where the curve crosses 10%,
     │                   ╲           interpolated on log10(PER)
  0% ┤                    ╰━━━━
     └────────────────────────── SNR (dB)
```

Every point uses the same seed, so it has the same data and the same noise,
only scaled. A higher SNR is the same run with quieter noise, which keeps each
curve close to monotonic without many more frames.

Each point in the JSON also has the receiver's counters, from `sim`'s
`metrics.frames`, which say where the frames went: `detected`, `sync_lost`,
`signal_failed` and `crc_failed`.

## The baseline

Measured with the guardrail's sweep (0.5 dB steps, 1000 frames, seed 1). At
the points checked, x86 and aarch64 lost exactly the same frames, so one
baseline serves both:

| Rate      | SNR at 10% PER |
|-----------|----------------|
| bpsk-1/2  | 18.08 dB       |
| bpsk-2/3  | 17.87 dB       |
| bpsk-3/4  | 18.02 dB       |
| qpsk-1/2  | 18.05 dB       |
| qpsk-2/3  | 18.38 dB       |
| qpsk-3/4  | 17.71 dB       |
| qam16-1/2 | 18.25 dB       |
| qam16-2/3 | 17.95 dB       |
| qam16-3/4 | 18.00 dB       |
| qam64-2/3 | 19.71 dB       |
| qam64-3/4 | 20.94 dB       |

Every rate up to 16-QAM needs about 18 dB, where a good receiver decodes BPSK
1/2 at a few dB. The counters show why: at 18 dB, not one of their frames
fails its CRC. Every frame they lose is lost in `timing_sync`, which misses
the long training symbols (see its `\bug`s). Only 64-QAM is limited by its
decoding.

## The PER tests

[tests/test_per.py](../../tests/test_per.py) checks the receiver against the
baseline. The tests take a minute or two, so they're in `tests/`, with the
other tests of `fun-ofdm`, which `uv run pytest` doesn't run unless asked. CI
runs them in a job of their own on each architecture
([ci.yml](../../.github/workflows/ci.yml)).

```bash
bazel run install
uv run pytest tests
```

- **A fixed point per rate:** 2000 frames at the SNR where the baseline lost 1%
  to 2%, with a different seed from the sweep that chose it. The PER's 99%
  upper bound must be under 5%, which fails a regression of about 1.5 dB.
- **Monotonic:** no rate's PER may rise with SNR, beyond chance.
- **SNR at 10% PER:** no rate may need more than 0.25 dB more SNR than its
  baseline to reach 10% PER. When a change makes a rate worse on purpose,
  update its baseline in `BASELINE_SNR_DB` and explain why in the release
  notes. When a change makes a rate better, lower its baseline, so the
  guardrail keeps the gain.

The tests use fixed seeds, so each gives the same answer every run until the
receiver changes.
