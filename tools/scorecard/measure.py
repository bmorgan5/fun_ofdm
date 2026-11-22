"""Measures what the scorecard reports that needs no USRP.

Each measurement writes a result directory, as tools/results/result.py says,
for scorecard.py to read (see README.md):

    uv run tools/scorecard/measure.py tests --out tests/
    uv run tools/scorecard/measure.py bench --out bench/ --cpus 1-3
    uv run tools/scorecard/measure.py latency --out latency/ --cpus 1-3

- `tests`: the unit tests and line coverage of src/, from `bazel run
  coverage`. They're exact, so they run once.
- `bench`: the receiver's throughput, and each block's cost, from
  fun-ofdm-bench, interleaved, 20 times each.
- `latency`: latency and each block's jitter, from `fun-ofdm sim`, 20 times.

-i walks through preparing the machine first (hostcheck.py).
"""

import argparse
import dataclasses
import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections.abc import Callable, Iterable, Sequence
from typing import Any

import hostcheck
import scorecard
from scorecard import BLOCKS, Metric, Rows

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "results"))

import result

#: The sim runs for latency and jitter: W-6's and W-54's rate and SNR, with
#: 1000-byte payloads, at 20 MS/s. sim sends frames back to back, as fast as
#: the receiver takes them, so these are the latencies of a receiver that's
#: busy all the time.
SIM_RUNS = {"W-6": ("bpsk-1/2", 20.0), "W-54": ("qam64-3/4", 30.0)}
SIM_PACKETS = 1000
SIM_PAYLOAD_SIZE = 1000
SIM_BANDWIDTH_MHZ = 20
SIM_SEED = 1

#: The latency percentiles the scorecard reports, by their names in sim's JSON.
PERCENTILES = {"p50": "p50", "p99": "p99", "p99.9": "p99_9", "max": "max"}

#: Repetitions of each benchmark and sim run, for the median and the
#: interquartile range.
DEFAULT_REPETITIONS = 20


# Tests ---------------------------------------------------------------------


def line_coverage(lcov: str, prefix: str = "src/") -> tuple[int, int]:
    """The lines covered and the lines in an lcov report, under a directory.

    The tests' helpers, test_util.h, aren't code under test, so they're left
    out, as tools/coverage/coverage.sh leaves them out of its report.
    """
    covered = total = 0
    counting = False
    for line in lcov.splitlines():
        if line.startswith("SF:"):
            path = line[3:]
            counting = path.startswith(prefix) and not path.endswith(
                "test_util.h"
            )
        elif counting and line.startswith("LF:"):
            total += int(line[3:])
        elif counting and line.startswith("LH:"):
            covered += int(line[3:])
    return covered, total


def count_tests(reports: Iterable[str]) -> int:
    """The tests run, from GoogleTest's XML reports, leaving out disabled."""
    count = 0
    for report in reports:
        suites = ET.fromstring(report)
        count += int(suites.get("tests", 0)) - int(suites.get("disabled", 0))
    return count


def measure_tests() -> Rows:
    """Runs the C++ tests with coverage, which needs Bazel."""
    start = datetime.datetime.now().timestamp()
    subprocess.run(
        # Every test runs, so each writes a report this run counts
        [
            "bazel",
            "run",
            "//:coverage",
            "--",
            "//...",
            "--nocache_test_results",
        ],
        cwd=REPO,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    covered, total = line_coverage(
        (REPO / "out" / "coverage" / "coverage.dat").read_text()
    )
    # Only the reports this run wrote, not ones from deleted tests.
    reports = [
        path.read_text()
        for path in (REPO / "bazel-testlogs" / "src").rglob("test.xml")
        if path.stat().st_mtime >= start
    ]
    return {
        "tests": {
            "tests": Metric("tests", False, [count_tests(reports)]),
            "line coverage (%)": Metric(
                "%", False, [100 * covered / total] if total else []
            ),
        }
    }


# Throughput and each block's cost ------------------------------------------


def bench_rows(results: dict[str, Any]) -> Rows:
    """Throughput and each block's cost, from fun-ofdm-bench's JSON.

    Throughput is receiver_chain's MS/s on each workload, and each block's
    cost is its ns per sample, for each repetition.
    """
    rows: Rows = {}
    for found in results["benchmarks"]:
        if found.get("run_type") != "iteration":
            continue
        block, workload = found["run_name"].split("/")[:2]
        if block == "receiver_chain":
            metric = rows.setdefault("throughput", {}).setdefault(
                workload, Metric("MS/s", False, [])
            )
            metric.samples.append(found["samples_per_second"] / 1e6)
        else:
            metric = rows.setdefault("block-cost", {}).setdefault(
                f"{block} {workload}", Metric("ns/sample", True, [])
            )
            metric.samples.append(found["time_per_sample"] * 1e9)
    return scorecard.in_order(rows)


def run_bench(
    bench: pathlib.Path,
    repetitions: int,
    workload_seconds: float | None,
    out: pathlib.Path,
    extra: Sequence[str] = (),
) -> dict[str, Any]:
    """Runs fun-ofdm-bench, and returns its JSON, which it writes to out.

    extra is more of its options, like --benchmark_filter.
    """
    command = [
        str(bench),
        f"--benchmark_repetitions={repetitions}",
        # Each repetition at a different time, interleaved with the other
        # benchmarks', so a slow minute doesn't land on one benchmark.
        "--benchmark_enable_random_interleaving=true",
        f"--benchmark_out={out}",
        "--benchmark_out_format=json",
    ]
    if workload_seconds is not None:
        command.append(f"--workload_seconds={workload_seconds}")
    command += extra
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    return json.loads(out.read_text())


def measure_bench(
    bench: pathlib.Path,
    repetitions: int,
    workload_seconds: float | None,
    extra: Sequence[str] = (),
) -> Rows:
    """Measures throughput and each block's cost with fun-ofdm-bench."""
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "bench.json"
        return bench_rows(
            run_bench(bench, repetitions, workload_seconds, out, extra)
        )


# Latency and jitter --------------------------------------------------------


def sim_rows(runs: dict[str, list[dict[str, Any]]]) -> Rows:
    """Latency and jitter from sim's JSON summaries, in µs, per repetition.

    Latency is its percentiles. Jitter is each block's processing time, and
    its jitter: how far each time's p99.9 is from its p50, for the
    processing time, the interval between starts and the wait to start, and
    the most the block fell behind (lag's max), its backlog.

    Args:
        runs: Each run's summaries, one per repetition, by the run's name.
    """
    rows: Rows = {"latency": {}, "jitter": {}}

    def add(row: str, name: str, nanoseconds: float) -> None:
        metric = rows[row].setdefault(name, Metric("µs", True, []))
        metric.samples.append(nanoseconds / 1000)

    for run, summaries in runs.items():
        for summary in summaries:
            metrics = summary["metrics"]
            for label, key in PERCENTILES.items():
                add("latency", f"{run} {label}", metrics["latency"][key])
            for block in BLOCKS:
                times = metrics["blocks"][block]
                processing = times["processing"]
                add(
                    "jitter", f"{run} {block} processing p50", processing["p50"]
                )
                for name in ("processing", "start_interval", "wait"):
                    spread = times[name]["p99_9"] - times[name]["p50"]
                    add("jitter", f"{run} {block} {name} jitter", spread)
                add("jitter", f"{run} {block} backlog", times["lag"]["max"])
    return rows


#: The significant digits merged_histograms() keeps of each bucket's value:
#: HdrHistogram's 3 make thousands of buckets, and a chart needs fewer.
BUCKET_DIGITS = 2


def coarse(value: int) -> int:
    """A bucket's value rounded up to BUCKET_DIGITS significant digits."""
    if value <= 0:
        return value
    step = 10 ** max(0, len(str(value)) - BUCKET_DIGITS)
    return -(-value // step) * step


def add_buckets(
    total: list[list[int]], buckets: Iterable[Sequence[int]]
) -> list[list[int]]:
    """Two histograms' buckets added together, as [value, count] pairs.

    Each value is the largest its bucket holds, rounded up to coarse(), so
    buckets from any summary line up.
    """
    counts = {value: count for value, count in total}
    for value, count in buckets:
        key = coarse(value)
        counts[key] = counts.get(key, 0) + count
    return [[value, counts[value]] for value in sorted(counts)]


def merged_histograms(
    runs: dict[str, list[dict[str, Any]]],
) -> dict[str, Any]:
    """Each run's histograms, every repetition's buckets added together.

    The charts draw each block's times from them. Keeping every summary
    instead would take about 400 kB a repetition, so the buckets are coarser
    too (add_buckets()).
    """
    merged: dict[str, Any] = {}
    for run, summaries in runs.items():
        histograms: dict[str, Any] = {"latency": [], "blocks": {}}
        for summary in summaries:
            metrics = summary["metrics"]
            histograms["latency"] = add_buckets(
                histograms["latency"], metrics["latency"]["buckets"]
            )
            for block, times in metrics["blocks"].items():
                mine = histograms["blocks"].setdefault(block, {})
                for name, histogram in times.items():
                    mine[name] = add_buckets(
                        mine.get(name, []), histogram["buckets"]
                    )
        merged[run] = histograms
    return merged


def sim_command(fun_ofdm: pathlib.Path, rate: str, snr_db: float) -> list[str]:
    """The sim command for one of SIM_RUNS."""
    return [
        str(fun_ofdm),
        "sim",
        "--phy-rate",
        rate,
        "--snr",
        f"{snr_db:g}",
        "--packets",
        str(SIM_PACKETS),
        "--payload-size",
        str(SIM_PAYLOAD_SIZE),
        "--bandwidth",
        str(SIM_BANDWIDTH_MHZ),
        "--seed",
        str(SIM_SEED),
        "--json",
    ]


def run_sims(
    fun_ofdm: pathlib.Path,
    repetitions: int,
    progress: Callable[[str], None],
) -> dict[str, list[dict[str, Any]]]:
    """Runs each of SIM_RUNS repetitions times, and returns the summaries."""
    runs: dict[str, list[dict[str, Any]]] = {}
    for run, (rate, snr_db) in SIM_RUNS.items():
        for i in range(repetitions):
            progress(f"sim {run} ({rate}), {i + 1} of {repetitions}")
            done = subprocess.run(
                sim_command(fun_ofdm, rate, snr_db),
                check=False,
                capture_output=True,
                text=True,
            )
            # sim exits with 1 when it loses frames, which the latency
            # doesn't depend on.
            if done.returncode not in (0, 1):
                raise subprocess.CalledProcessError(
                    done.returncode, done.args, done.stdout, done.stderr
                )
            runs.setdefault(run, []).append(json.loads(done.stdout))
    return runs


# Preparing the machine -----------------------------------------------------


def suggested_cpus(host: hostcheck.Host) -> str:
    """The CPUs to pin to: the isolated ones, or every one but CPU 0.

    CPU 0 usually handles the most interrupts and housekeeping.
    """
    if host.isolated:
        return host.isolated
    return hostcheck.format_cpu_list(range(1, host.cpus_online))


def prepare(fun_ofdm: pathlib.Path, cpus: str | None) -> str | None:
    """Walks through preparing the machine, and returns the CPUs to pin to.

    For each warning, says how to fix it and waits, then checks again.
    """
    host = hostcheck.collect(fun_ofdm=fun_ofdm)
    # The warnings come next, one at a time.
    print(hostcheck.report(dataclasses.replace(host, warnings=[])), "\n")
    for warning in host.warnings:
        print(f"- {warning}")
        if "isn't idle" in warning:
            print("  The busiest processes:")
            busiest = hostcheck.run_command(
                ["ps", "-eo", "pid,pcpu,comm", "--sort=-pcpu"]
            )
            for line in (busiest or "").splitlines()[:6]:
                print(f"    {line}")
        input("  Fix it, or not, then press Enter. ")
    if cpus is None:
        suggestion = suggested_cpus(host)
        answer = input(
            f"Pin fun-ofdm to which CPUs? [{suggestion}, or 'none'] "
        ).strip()
        cpus = None if answer == "none" else (answer or suggestion)
    if cpus is not None:
        os.sched_setaffinity(0, hostcheck.parse_cpu_list(cpus))
    host = hostcheck.collect(fun_ofdm=fun_ofdm)
    print("\n" + hostcheck.report(host))
    if input("\nStart measuring? [Y/n] ").strip().lower() == "n":
        sys.exit(1)
    return cpus


# Main ----------------------------------------------------------------------


#: Each measurement's title, for result.md.
TITLES = {
    "tests": "Unit tests and line coverage of src/",
    "bench": "Receiver throughput, and each block's cost",
    "latency": "Latency and jitter in simulation",
}


def rows_json(rows: Rows) -> dict[str, Any]:
    """Rows as JSON, each metric with every repetition's value."""
    return {
        row: {name: metric.to_json() for name, metric in metrics.items()}
        for row, metrics in rows.items()
    }


def body(rows: Rows) -> str:
    """result.md's tables: each row's grid of medians and quartiles."""
    lines: list[str] = []
    for row, metrics in rows.items():
        lines += ["", f"## {scorecard.TITLES[row]}", ""]
        lines += scorecard.grid(row, metrics)
    return "\n".join(lines).strip() + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    """Runs a measurement from the command line."""
    bin_dir = hostcheck.DEFAULT_FUN_OFDM.parent
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("measurement", choices=list(TITLES))
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        required=True,
        metavar="DIR",
        help="write result.json and result.md to DIR",
    )
    parser.add_argument(
        "--cpus",
        help="pin everything to these CPUs, like 1-3 (the Pinned "
        "configuration needs it)",
    )
    parser.add_argument("--repetitions", type=int, default=DEFAULT_REPETITIONS)
    parser.add_argument(
        "--filter",
        metavar="REGEX",
        help="bench: only the benchmarks that match, like ^receiver_chain/ "
        "for the throughput alone (default: every one)",
    )
    parser.add_argument(
        "--trace",
        type=pathlib.Path,
        metavar="FILE",
        help="bench: record a trace of the benchmarks to FILE, to measure "
        "what tracing costs; it grows by about 2.3 MB a second at 20 MS/s",
    )
    parser.add_argument(
        "--workload-seconds",
        type=float,
        help="shorter benchmark workloads for a quick try (default: the "
        "standard 10 s)",
    )
    parser.add_argument(
        "-i",
        "--interactive",
        action="store_true",
        help="walk through preparing the machine first",
    )
    parser.add_argument("--bin-dir", type=pathlib.Path, default=bin_dir)
    args = parser.parse_args(argv)

    fun_ofdm = args.bin_dir / "fun-ofdm"
    bench = args.bin_dir / "fun-ofdm-bench"
    binaries = {
        "tests": {},
        "bench": {"fun-ofdm-bench": bench},
        "latency": {"fun-ofdm": fun_ofdm},
    }[args.measurement]
    for binary in binaries.values():
        if not binary.exists():
            parser.error(f"{binary} doesn't exist: bazel run install")
    if args.measurement == "tests" and shutil.which("bazel") is None:
        parser.error("the tests need Bazel")
    if args.repetitions < 1:
        parser.error("--repetitions must be at least 1")
    if args.out.exists() and not args.out.is_dir():
        parser.error(f"--out {args.out} isn't a directory: it takes one")

    cpus = args.cpus
    if args.interactive:
        cpus = prepare(fun_ofdm, cpus)
    elif cpus is not None:
        # Children inherit it: sim, fun-ofdm-bench and their threads.
        os.sched_setaffinity(0, hostcheck.parse_cpu_list(cpus))
    host = hostcheck.collect(fun_ofdm=fun_ofdm)
    if not args.interactive:
        print(hostcheck.report(host), "\n", file=sys.stderr)

    def progress(text: str) -> None:
        print(f"... {text}", file=sys.stderr, flush=True)

    options: dict[str, Any] = {}
    if args.measurement != "tests":
        options["repetitions"] = args.repetitions
    if args.measurement == "bench":
        options["workload_seconds"] = args.workload_seconds
        options["filter"] = args.filter
        options["trace"] = args.trace is not None
    # Before measuring, which it describes. The tests build from the
    # sources, which the commit in the host pins down.
    prov = result.provenance(
        f"tools/scorecard/measure.py {args.measurement}",
        options,
        binaries,
        host=host,
        sources=[pathlib.Path(__file__)],
    )
    extra: dict[str, Any] = {}
    if args.measurement == "tests":
        progress("The tests, with coverage")
        rows = measure_tests()
    elif args.measurement == "bench":
        progress("fun-ofdm-bench: about 35 minutes with the defaults")
        bench_options = []
        if args.filter:
            bench_options.append(f"--benchmark_filter={args.filter}")
        if args.trace:
            bench_options.append(f"--trace={args.trace}")
        rows = measure_bench(
            bench, args.repetitions, args.workload_seconds, bench_options
        )
    else:
        runs = run_sims(fun_ofdm, args.repetitions, progress)
        rows = sim_rows(runs)
        extra["histograms"] = merged_histograms(runs)
    result.write(
        args.out,
        prov,
        {"rows": rows_json(rows), **extra},
        TITLES[args.measurement],
        body(rows),
    )
    print(body(rows), end="")
    progress(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
