# Scorecard

The same measurements every time, so a release's numbers compare directly with
the last one's. Each measurement tool writes a directory of results
([the result contract](../results/result.py)), and
[scorecard.py](scorecard.py) reads a directory of them and writes the
scorecard: each tool's headline numbers, with medians and interquartile
ranges. It runs nothing itself. [hostcheck.py](hostcheck.py) records the
machine, in every result.

| Row            | What                                                 | From                                           | Runs |
|----------------|------------------------------------------------------|------------------------------------------------|------|
| `tests`        | Unit tests, and line coverage of `src/`              | `measure.py tests`: `bazel run coverage`        | 1    |
| `throughput`   | The receiver's MS/s on W-idle, W-6 and W-54          | `measure.py bench`: `fun-ofdm-bench`            | 20   |
| `block-cost`   | Each block's ns per sample on its own, per workload  | `measure.py bench`: `fun-ofdm-bench`            | 20   |
| `latency`      | p50, p99, p99.9 and max, in µs                       | `measure.py latency`: `fun-ofdm sim`            | 20   |
| `jitter`       | Each block's processing time, jitter and backlog, µs | `measure.py latency`: `fun-ofdm sim`            | 20   |
| `per`          | Each rate's SNR at 10% PER, in simulation            | [`tools/per/sweep.py`](../per/README.md)        | 1    |
| `realtime`     | The highest bandwidth kept up with, on a B210        | [`hardware_sweep.py realtime`](../hardware/README.md) | 3 |
| `per-hardware` | Each rate's SNR at 10% PER on a B210, and its loss   | [`hardware_sweep.py per`](../hardware/README.md) | 1    |
| `link`         | Frames received intact in the link smoke test        | [`uv run pytest hil`](../../hil/README.md)      | 1    |

The tests are exact, and the PER sweep uses fixed seeds, so those run once.
The benchmarks and latency are times, so each is the median of 20 runs, with
the interquartile range.

## Measuring

```bash
bazel run install
uv run tools/scorecard/hostcheck.py        # The machine, and what to fix first
uv run tools/scorecard/measure.py tests --out results/tests
uv run tools/scorecard/measure.py bench --out results/bench -i      # About 50 minutes on a Pi 5
uv run tools/scorecard/measure.py latency --out results/latency -i
uv run tools/per/sweep.py --snr-min 14 --snr-max 24 --snr-step 0.5 --frames 1000 --out results/per
uv run tools/scorecard/charts.py results
uv run tools/scorecard/scorecard.py results
```

`measure.py latency` also runs each workload once more with a trace, which
doesn't count towards the latency, for `charts.py`. `-i` walks through
preparing the machine, then asks which CPUs to pin to;
`--cpus` pins without asking. `--repetitions` and `--workload-seconds` make a
quick try, which isn't a result to keep. `--help` lists the rest.

`scorecard.py DIR` reads `DIR/*/result.json`, each row from one result, and
writes `DIR/scorecard.md` and `DIR/scorecard.json`. Only `DIR`'s own
subdirectories are read, so a second set of results inside it, like a repeat,
stays apart. It refuses a sweep that was interrupted, and notes results from
different binaries or commits.

**Charts:** `charts.py DIR` draws them from the same results, to
`DIR/charts/`: PER vs SNR on hardware and in simulation, each block's
processing time at each percentile, its start interval and wait, a timeline of
each block's runs from the realtime sweep's traces, and the realtime sweep's
overflows, latency and each CPU's use against bandwidth. `DIR/charts/charts.md`
shows them, and checks that each traced run's histograms agree with its trace,
where the trace has every run. Run it before `scorecard.py`,
which links them. It uses Perfetto's trace processor, which downloads its
binary the first time.

**Comparing:** `--compare OLD` adds each metric's change since an earlier
directory to the tables. A repeated metric changed only if Mann-Whitney U's p
is below 0.05: the runs are too noisy to trust a difference in medians alone.
One measured once changed if it moved more than its row allows: nothing for
the tests and coverage, 0.25 dB for an SNR in simulation (the guardrail's
limit), 1 dB on hardware, and 1 point for the link test.

## Preparing the machine

Numbers from a busy machine, or one that changes its own clocks, aren't worth
comparing. A configuration is how the machine is set up: there are four, each
adding to the one before. They aren't settings of fun-ofdm or of these
scripts. You set the machine up, and hostcheck works out which configuration
it's in and warns about what's left. `-i` walks through its warnings, then
asks which CPUs to pin to.

| Configuration | What's set up                                                                     | Where                                             |
|---------------|-----------------------------------------------------------------------------------|---------------------------------------------------|
| Stock         | Nothing                                                                           |                                                   |
| Pinned        | fun-ofdm pinned to some CPUs, every CPU on the `performance` governor, turbo off  | `--cpus` or `taskset`, and `/sys`, until a reboot |
| Isolated      | The pinned CPUs kept free of other work, the timer tick and interrupts            | The kernel's command line, then a reboot          |
| RT            | A `PREEMPT_RT` kernel, and real-time thread priorities                            | Installing that kernel                            |

These are examples, for CPUs 2 to 7 of an 8-core x86 machine and CPUs 1 to 3
of a Raspberry Pi 5: pick the CPUs for yours.

**x86:**

```bash
# Pinned, until the next reboot. Then run with --cpus 2-7.
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo  # Intel turbo
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost          # AMD boost

# Isolated: add this to GRUB_CMDLINE_LINUX in /etc/default/grub, then run
# sudo update-grub and reboot. Interrupts go to CPUs 0 and 1.
isolcpus=2-7 nohz_full=2-7 irqaffinity=0-1
```

**Raspberry Pi 5:** it has no turbo setting, but it slows down when it gets
hot, so it needs a cooler; hostcheck warns if it has been throttled. With 4
cores, the receiver's 7 threads always share them.

```bash
# Pinned, until the next reboot. Then run with --cpus 1-3.
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Isolated: add this to the end of the one line in /boot/firmware/cmdline.txt,
# and reboot. Interrupts go to CPU 0.
isolcpus=1-3 nohz_full=1-3 irqaffinity=0
```

Measure throughput and block cost on Pinned or later. Tuning mostly changes
latency's tail, not its typical value, so it's worth measuring on each
configuration, each in a directory of its own.

In a VM or WSL, the hypervisor owns the clocks and shares the CPUs, so the
numbers only compare with others from the same machine.

## What the numbers mean

- **Throughput and block cost** are `fun-ofdm-bench`'s
  ([src/bench](../../src/bench/README.md)), with each repetition interleaved
  with the other benchmarks', so a slow minute doesn't land on one of them.
- **Latency and jitter** come from `sim`'s histograms
  ([src/metrics](../../src/metrics/README.md)), sending 1000 frames of 1000
  bytes at 20 MS/s. sim sends samples as fast as the receiver takes them, so
  the latency is a busy receiver's. A block's jitter is its p99.9 minus its
  p50, and its backlog is the most it fell behind (`lag`'s max). The interval
  between a block's starts includes the time sim takes to make each batch.
- **The SNR at 10% PER** in simulation is from tools/per/sweep.py. With the
  guardrail's settings, it's the sweep `tests/test_per.py` checks, so the two
  agree.

## TODO

- **Pin each thread to its own CPU.** `--cpus` pins the whole process to a set
  of CPUs, and the kernel still moves each thread around within it.
- **Real-time priorities in `sim` and `fun-ofdm-bench`.** Only `rx` and `tx`
  ask for them, so the RT configuration only adds the kernel for now.
- **Check the interrupts.** hostcheck doesn't check that interrupts stay off
  the isolated CPUs.
- **Record the compiler flags.** The binary records its compiler, not its
  flags, so a result can't say whether it was an optimized build.
