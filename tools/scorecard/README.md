# Scorecard

The same measurements of the receiver every time, so a release's numbers
compare directly with the last one's. [scorecard.py](scorecard.py) runs them,
and [hostcheck.py](hostcheck.py) records the machine they ran on.

| Row          | What                                                  | From                                       | Runs |
|--------------|-------------------------------------------------------|--------------------------------------------|------|
| `tests`      | Unit tests, and line coverage of `src/`               | `bazel run coverage`                       | 1    |
| `throughput` | The receiver's MS/s on W-idle, W-6 and W-54           | `fun-ofdm-bench`: `receiver_chain`         | 20   |
| `block-cost` | Each block's ns per sample on its own, per workload   | `fun-ofdm-bench`: each block               | 20   |
| `latency`    | p50, p99, p99.9 and max, in µs                        | `fun-ofdm sim`, at W-6's and W-54's rates  | 20   |
| `jitter`     | Each block's processing time, jitter and backlog, µs  | `fun-ofdm sim`, as `latency`               | 20   |
| `per`        | Each rate's SNR at 10% PER                            | The [PER](../per/README.md) guardrail's sweep | 1 |

The tests are exact, and the PER sweep uses fixed seeds, so those run once.
The rest are times, so each is the median of 20 runs, with the interquartile
range.

## Running it

```bash
bazel run install
uv run tools/scorecard/hostcheck.py        # The machine, and what to fix first
uv run tools/scorecard/scorecard.py --milestone milestone2 -i
```

It takes about 40 minutes, most of it the benchmarks. `--rows` runs only some
rows, and `--repetitions` and `--workload-seconds` make a quick try, which
isn't a scorecard. `--help` lists the rest.

It writes `docs/perf/<milestone>/<host>/<configuration>.json`, with every
run's number, and a `.md` with the tables. The host is the board or CPU, like
`intel-core-i7-10700k`, unless `--host` names it.

**Comparing:** `--compare <old.json>` adds each metric's change to the table.
A repeated metric changed only if Mann-Whitney U's p is below 0.05: the runs
are too noisy to trust a difference in medians alone. The tests and coverage
may never drop, and an SNR may get up to 0.25 dB worse, the guardrail's limit.

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

The throughput and block-cost rows are measured on Pinned or later. Latency
and jitter are measured on each configuration, since tuning mostly changes
the tail, not the typical value.

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
- **The SNR at 10% PER** is from the sweep `tests/test_per.py` checks, so the
  two agree.

## TODO

- **Pin each thread to its own CPU.** `--cpus` pins the whole process to a set
  of CPUs, and the kernel still moves each thread around within it.
- **Real-time priorities in `sim` and `fun-ofdm-bench`.** Only `rx` and `tx`
  ask for them, so the RT configuration only adds the kernel for now.
- **Check the interrupts.** hostcheck doesn't check that interrupts stay off
  the isolated CPUs.
- **Record the compiler flags.** The binary records its compiler, not its
  flags, so a result can't say whether it was an optimized build.
