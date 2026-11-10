# Metrics

The receiver measures itself as it runs, in `rx`, `sim` and `fun-ofdm-bench`.
Three numbers say how it performs:

1. **Throughput:** samples processed per second. At 20 MS/s or more, a 20 MHz
   channel keeps up. `fun-ofdm-bench` measures it ([bench](../bench/README.md)).
2. **Latency:** from a frame's last sample arriving to its payload coming out.
3. **Jitter:** how irregularly the samples arrive and each block runs.

This directory has the pieces they're measured with: a clock
([metrics.h](metrics.h)), histograms ([histogram.h](histogram.h)), and traces
([tracing.h](tracing.h)). `receiver_chain` and `receiver` do the measuring.

## What's measured

Every batch of samples is timestamped at each point of the pipeline:

```text
 USRP ─► recv() ─► process_samples() ─► 6 blocks, each on its thread ─► payloads
           │            │                  │
       arrival        ready          start ─ work() ─ end
```

| Measured           | From → to                                     | Where      |
|--------------------|-----------------------------------------------|------------|
| `arrival_interval` | One batch's arrival → the next's              | The chain  |
| `recv_delay` (rx)  | The batch's time on the USRP → on the host    | `receiver` |
| `wait`             | Ready → the block starts: scheduling jitter   | Each block |
| `processing`       | The block's `work()`: its cost                | Each block |
| `start_interval`   | One start → the next: timing jitter           | Each block |
| `lag`              | The batch's arrival → the block ends: backlog | Each block |
| `latency`          | A frame's last sample arrives → payload out   | The chain  |

[receiver_chain.h](../phy/receiver_chain.h) draws how they fit together, and
[receiver.h](../receiver.h) explains `recv_delay`.

And counters: frames `detected` (on noise alone, false alarms), `sync_lost`
(no long training symbols found), `signal_failed` (the SIGNAL symbol's parity
or RATE was bad), `crc_ok` and `crc_failed` per rate, and the USRP's
`overflows` (rx).

## Histograms

Each time goes into an [HdrHistogram](https://github.com/HdrHistogram/HdrHistogram_c),
which holds times from 1 ns to an hour, each to within 0.1%.

- **Always on,** for runs of any length, live ones included. Recording takes a
  few nanoseconds.
- **One set per thread,** created with the chain: each block's thread records
  into its own, and the caller's thread into the chain's, so recording needs
  no locks and doesn't allocate. `receiver_chain::metrics()` copies them once
  the threads are idle.
- **About 270 KB each,** so 7 MB for a chain's 26.

`--json` writes them in the summary's `metrics` section
([summary.h](../summary.h) lists its members): `count`, `p50`, `p99`, `p99_9`
and `max`, in nanoseconds, then every bucket that holds any values, so scripts
can add runs together:

```text
p50      the typical value; it says nothing about jitter
p99 − p50, p99.9 − p50, max − p50
         the jitter: the spread, not the standard deviation, which hides the tail
```

Percentiles lose the order of events, so they can't tell slow batches spread
out from a burst of them. `lag`, and traces, can.

## Traces

A trace records every one of those timestamps as an event, in order, with its
thread, using the [Perfetto](https://perfetto.dev) SDK. It shows bursts and
backlogs, which histograms can't. Traces are off unless asked for, and grow by
about 2.3 MB per second of 20 MS/s samples, so they're for runs of minutes.

```text
main thread     ▼arrival ▼ready                ▼arrival ▼ready        ▼payload
frame_detector           [frame_detector]               [frame_detector]
timing_sync              [timing_sync   ]               [timing_sync   ]
...                      ...
frame_decoder            [frame_decoder ]               [frame_decoder      ]
```

**Into a file:** `--trace FILE` (`rx` and `sim`, or `--trace=FILE` for
`fun-ofdm-bench`). Open it in [ui.perfetto.dev](https://ui.perfetto.dev), or
query it with Perfetto's trace processor, which `scorecard.py` will use:

```bash
fun-ofdm sim --trace sim.pftrace
```

**With the kernel's scheduler events:** the thread switches, wake-ups and CPU
migrations, and each CPU's frequency, show why a thread stalled. Perfetto's
`traced` service records them, and `--trace-system` sends the receiver's
events to it too. `bootstrap.sh` installs Ubuntu's `perfetto` package, whose
services only run once started:

```bash
sudo systemctl start traced traced-probes
sudo perfetto --txt -c tools/trace/fun-ofdm.pbtxt -o rx.pftrace &  # 60 s
sudo fun-ofdm rx --trace-system
```

[tools/trace/fun-ofdm.pbtxt](../../tools/trace/fun-ofdm.pbtxt) is the
config: the receiver's events and the kernel's.

## What they cost

`--config=no_metrics` builds without the timestamps, histograms and traces;
the counters stay. Comparing `receiver_chain` on 2 s workloads, built without
metrics, with them, and with them and a trace, each 10 times, interleaved, on
an i7-10700K in Docker under WSL2 (not pinned, so not scorecard numbers):

| Workload | Without    | With histograms         | And a trace              |
|----------|------------|-------------------------|--------------------------|
| W-idle   | 32.30 MS/s | 31.55 (−2.3%)           | 31.25 (−3.3%)            |
| W-6      | 31.17 MS/s | 30.30 (−2.8%, p = 0.03) | 29.65 (−4.9%, p = 0.002) |
| W-54     | 20.02 MS/s | 19.87 (−0.8%)           | 19.05 (−4.8%, p < 0.001) |

The medians, and Mann-Whitney U's p where the difference is significant at
5%. The histograms cost up to about 3%, and a trace about 5%.

```bash
bazel run --config=no_metrics fun-ofdm-bench -- --benchmark_filter=receiver_chain
bazel run fun-ofdm-bench -- --benchmark_filter=receiver_chain
bazel run fun-ofdm-bench -- --benchmark_filter=receiver_chain --trace=bench.pftrace
```
