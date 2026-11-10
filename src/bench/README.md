# Benchmarks

`fun-ofdm-bench` measures how fast the receiver runs on three standard
workloads, using [google_benchmark](https://github.com/google/benchmark). It
answers two questions:

- **Does the receiver keep up?** `receiver_chain/<workload>` runs the whole
  multithreaded receiver on 10 s of samples and times it on the wall clock.
  A 20 MHz channel delivers 20 MS/s, so real time needs at least that
  (scorecard row S2).
- **Where does the time go?** `<block>/<workload>`, like
  `frame_detector/W-54`, runs one of the six blocks on its own, on one thread
  (scorecard row S3).

`bazel run install` copies the binary next to `fun-ofdm`, built with the same
toolchain and flags, so a machine without Bazel, like the Pi, runs what's
released.

## Running it

```bash
bazel run fun-ofdm-bench                       # Everything, about 1.5 min on x86
bazel run fun-ofdm-bench -- --benchmark_filter=W-54
bazel run fun-ofdm-bench -- --benchmark_repetitions=20 \
  --benchmark_out=bench.json --benchmark_out_format=json
```

`--help` lists google_benchmark's flags and the binary's own two,
`--workload_seconds` and `--stage_seconds`. Write JSON to a file with
`--benchmark_out`, not to stdout, where the receiver prints a line for each
frame that fails its CRC check.

## How google_benchmark runs a benchmark

A benchmark is a function with a loop. google_benchmark times the loop, and
picks how many times it goes round:

```text
bench_x(state) {
  setup                  ◄── not timed
  for (auto _ : state)   ◄── timed: each pass is one iteration
    work
}

run 1: 1 iteration      too short? ──► run again with more iterations
run 2: 100 iterations   ...until the loop takes 0.5 s (--benchmark_min_time)
report: time / iterations, and the counters
```

`--benchmark_repetitions=N` does all of that N times and adds the median,
mean and spread. Inside the loop, `state.PauseTiming()` and `ResumeTiming()`
stop and restart the clock. All the benchmarks here time the wall clock
(`UseRealTime()`, which adds `/real_time` to their names), since the chain's
work happens on threads of its own.

## The workloads

[workload.h](workload.h) defines them. Each is 10 s of samples at 20 MS/s,
200 million samples, as a USRP would deliver them: frames separated by idle
time, with noise throughout.

```text
         idle          frame          idle          frame
     ┌────────────┬──────────────┬────────────┬──────────────┬─ ─ ─
     │   noise    │ preamble,    │   noise    │              │
     │            │ SIGNAL, data │            │              │
     └────────────┴──────────────┴────────────┴──────────────┴─ ─ ─
      ◄─ idle ───► ◄── frame ───►
        samples       samples       airtime = frame / (idle + frame)
```

| Workload | Frames                               | Airtime | SNR   |
|----------|--------------------------------------|---------|-------|
| W-idle   | none: noise only                     | 0%      | 20 dB |
| W-6      | 1000 bytes at 6 Mbit/s (BPSK 1/2)    | 50%     | 20 dB |
| W-54     | 1000 bytes at 54 Mbit/s (64-QAM 3/4) | 90%     | 30 dB |

A real receiver mostly hears noise, so W-idle is the cost of the detector
that always runs. W-6's frames are long and slow to decode, and W-54 keeps the
decoder busiest.

`workload_source` makes the samples from a seed, the same on every machine,
so nothing is stored. Every workload uses the same seed, `WORKLOAD_SEED`:
changing it would move the baseline.

```text
            seed
     ┌────────┴────────┐
     ▼                 ▼
random_source    channel (src/channel.h)
 payload bytes    noise, at the SNR
     │                 │
     ▼                 │
frame_builder ──► channel.transmit(frame) ──┐
                  channel.idle(n) ──────────┴──► read(n): the next n samples
```

Each payload is a 4-byte sequence number, then random bytes, so the receiver's
`reassembler` can count each frame once (src/framing.h). The noise power is
relative to the OFDM signal's mean power, `OFDM_SIGNAL_POWER`, so W-idle has a
noise floor though it has no frames.

10 s of samples is 3.2 GB as `std::complex<double>`, so `read()` generates them
a piece at a time, and only the piece being read is in memory.

## `receiver_chain/<workload>`: the whole receiver

One iteration runs the whole workload through a fresh `receiver_chain`, in
batches of 4096 samples, the size `rx` reads from the USRP:

```text
          workload_source
                │ 1M samples at a time (256 batches)        clock paused
                ▼
          split into 4096-sample batches                    clock paused
                │
════════════════╪═════════════════════════════════════════════════════════
                ▼                                           clock running
   receiver_chain::process_samples(batch), once per batch
   ┌──────────────────────────────────────────────────────────────┐
   │ frame_detector → timing_sync → fft_symbols → channel_est     │
   │               → phase_tracker → frame_decoder                │
   │ six threads, each block on its own                           │
   └──────────────────────────────┬───────────────────────────────┘
                                  ▼
                 payloads → reassembler → frames_received
════════════════╪═════════════════════════════════════════════════════════
                ▼
       next 256 batches, until the workload ends; then zeros
       to flush the last frames out, with the clock paused
```

It reports:

- `samples_per_second` and `time_per_sample` (seconds; the console prints
  ns): the workload's samples over the wall-clock time the chain ran.
- `frames_sent` and `frames_received`: the workload's whole frames, and how
  many the receiver decoded. A receiver that stops decoding would otherwise
  post a good time.

## `<block>/<workload>`: one block on its own

Each block's input depends on the blocks before it, so the benchmark first
captures it, then replays it:

```text
1. Capture, once per block and workload (not timed)

   workload_source, first 0.5 s (--stage_seconds): 2441 batches
          │
          ▼
   pipeline (pipeline.h): the six blocks, one after another on one thread,
   with the same buffer swaps as receiver_chain::process_samples()
          │
          ▼ the block's input_buffer before each step
   captured batches  [b0][b1][b2] ... [b2440]

2. Benchmark (timed): one batch per iteration, round and round

   for (auto _ : state):
     block.input_buffer.swap(batch[i])    ◄── no copy: swapping two vectors
     block.work()                             only swaps their pointers
     block.input_buffer.swap(batch[i])
     i = (i + 1) % 2441
```

Since each step is one of the chain's calls, a block gets exactly the batches
it gets in `receiver_chain`, including the old ones that the chain's
empty-batch bug hands it again. `PipelineTest` checks that `pipeline` decodes
what `receiver_chain` decodes.

Each iteration counts 4096 workload samples, so `samples_per_second` and
`time_per_sample` mean the same as the chain's, even for the blocks after
`fft_symbols`, whose inputs are symbols. A block's time per sample is its share
of the chain's work. A block that has nothing to do on a workload, like
`frame_decoder` on W-idle, gets a meaningless number.

The captured batches stay in memory, up to a few hundred MB, until the next
benchmark captures its own. When the batches go round again, the block's state
jumps from the end of the 0.5 s back to its start, which can garble one frame
per pass.

## What's timed, and what isn't

| Work                                           | Timed?                    |
|------------------------------------------------|---------------------------|
| The receiver processing samples                | Yes                       |
| Generating the samples, splitting them up      | No: the clock is paused   |
| Creating and destroying the chain, its threads | No                        |
| Flushing the last frames out                   | No                        |
| Capturing a block's inputs                     | No: before the timed loop |
| Moving a batch into a block                    | Yes, but it's a swap      |

Pausing has two costs, measured on an i7-10700K with W-54:

- **The pause itself:** a `PauseTiming()`/`ResumeTiming()` pair takes about
  200 ns, and the chain benchmark pauses about 760 times per workload. That's
  under 0.2 ms in 10 s.
- **Disturbing the chain:** while the samples are generated, the chain's
  threads sleep and the caches fill with other data. With every sample
  generated before the loop, which takes 3.2 GB, the chain ran about 1% faster
  than it does here, however often it paused. So the number is about 1%
  pessimistic, and the same for every version, so comparisons between versions
  aren't affected. `rx` works the same way, reading from the USRP between
  batches.

## The machine

Benchmarks only mean something next to the machine that ran them. The header
google_benchmark prints is the kernel's view, which a container or VM changes:

```text
Run on (16 X 3792.01 MHz CPU s)   ◄── every CPU online, even ones the
                                      process can't use; the MHz is cpu0's
                                      clock, or a VM's nominal one
CPU Caches: ...                   ◄── from /sys
Load Average: 0.53, 0.80, 0.81    ◄── the whole kernel's, not the container's
```

So `fun-ofdm-bench` adds the rest to the context, which is also in the JSON
([host_info.h](host_info.h)):

| Key           | Example                                                       |
|---------------|---------------------------------------------------------------|
| `cpu_model`   | `Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz`, `ARM Cortex-A76` |
| `board`       | `Raspberry Pi 5 Model B Rev 1.0` (only with a device tree)    |
| `arch`        | `x86_64`, `aarch64`                                           |
| `kernel`      | `6.18.54.1-microsoft-standard-WSL2`                           |
| `cpus_online` | `16`: what google_benchmark counts                            |
| `cpus_usable` | The CPUs the process may run on (its affinity)                |
| `cpu_quota`   | `none`, or the cgroup's CPU limit, like `2.5`                 |
| `container`   | `docker`, `podman` or `none`                                  |

**In Docker,** `--cpuset-cpus` lowers `cpus_usable`, and `--cpus` sets
`cpu_quota`. Either prints a warning, because the chain runs seven threads
(six blocks and the caller), so a limit changes its throughput. On the
i7-10700K, W-54 ran at 20.2 MS/s on all 16 CPUs, 20.7 on 4 and 16.3 on 2.
Without a limit, a container runs at the machine's speed: its processes are
ordinary processes on the host's kernel.

The scorecard's `hostcheck.py` will record the rest: the governor, turbo,
isolated cores and the tuning configuration.

## Testing

`bench_smoke_test` runs every benchmark once on 50 ms workloads, and checks
that each ran and that W-6 and W-54 decoded frames. That's all CI does with
the benchmarks: shared runners are too noisy for real numbers.
