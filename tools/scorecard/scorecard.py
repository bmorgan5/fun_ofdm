"""Runs the scorecard: the same measurements of the receiver every time.

Each release's numbers come from here, so they compare directly with the last
release's (see README.md for the rows and how to run it well):

    uv run tools/scorecard/scorecard.py --milestone milestone2 --cpus 1-15

It runs hostcheck.py first, then each row, and writes JSON and a Markdown
table to docs/perf/<milestone>/<host>/<configuration>.{json,md}. With
--compare, the table also has each metric's change from an earlier run, and
whether it's significant.
"""

import argparse
import dataclasses
import datetime
import json
import math
import os
import pathlib
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections.abc import Callable, Iterable, Sequence
from typing import Any

import hostcheck

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "per"))

import sweep

#: The rows the scorecard measures, by the names --rows and the JSON use.
ROWS = ("tests", "throughput", "block-cost", "latency", "jitter", "per")

TITLES = {
    "tests": "Unit tests and line coverage of src/",
    "throughput": "Receiver throughput",
    "block-cost": "Each block's cost, on its own",
    "latency": "Latency, last sample of a frame in to its payload out",
    "jitter": "Jitter of each block",
    "per": "SNR at 10% PER",
}

#: The receiver's blocks, in order.
BLOCKS = (
    "frame_detector",
    "timing_sync",
    "fft_symbols",
    "channel_est",
    "phase_tracker",
    "frame_decoder",
)

#: fun-ofdm-bench's standard workloads (src/bench/workload.h).
WORKLOADS = ("W-idle", "W-6", "W-54")

#: The sim runs for latency and jitter: W-6's and W-54's rate and SNR, with 1000-byte
#: payloads, at 20 MS/s. sim sends frames back to back, as fast as the
#: receiver takes them, so these are the latencies of a receiver that's
#: busy all the time.
SIM_RUNS = {"W-6": ("bpsk-1/2", 20.0), "W-54": ("qam64-3/4", 30.0)}
SIM_PACKETS = 1000
SIM_PAYLOAD_SIZE = 1000
SIM_BANDWIDTH_MHZ = 20
SIM_SEED = 1

#: The latency percentiles the scorecard reports, by their names in sim's JSON.
PERCENTILES = {"p50": "p50", "p99": "p99", "p99.9": "p99_9", "max": "max"}

#: A difference is significant when Mann-Whitney U's p is below this.
SIGNIFICANCE = 0.05

#: Repetitions of each benchmark and sim run, for the median and the
#: interquartile range.
DEFAULT_REPETITIONS = 20

#: The SNR at 10% PER may get at most this much worse before the comparison
#: calls it worse,
#: as tests/test_per.py's guardrail allows.
PER_MAX_REGRESSION_DB = 0.25


@dataclasses.dataclass
class Metric:
    """One measured number: every repetition's value, and its unit.

    `lower_is_better` says which way is an improvement. A metric measured
    once, like the test count, has one sample; one that couldn't be measured,
    like an SNR a curve never reached, has none.
    """

    unit: str
    lower_is_better: bool
    samples: list[float]

    @property
    def median(self) -> float | None:
        """The median, or None without samples."""
        return statistics.median(self.samples) if self.samples else None

    @property
    def quartiles(self) -> tuple[float, float] | None:
        """The first and third quartiles, or None without samples."""
        if not self.samples:
            return None
        if len(self.samples) == 1:
            return self.samples[0], self.samples[0]
        q1, _, q3 = statistics.quantiles(self.samples, n=4, method="inclusive")
        return q1, q3

    def to_json(self) -> dict[str, Any]:
        """The metric as JSON, with its median and quartiles."""
        quartiles = self.quartiles
        return {
            "unit": self.unit,
            "lower_is_better": self.lower_is_better,
            "median": self.median,
            "q1": quartiles[0] if quartiles else None,
            "q3": quartiles[1] if quartiles else None,
            "samples": self.samples,
        }

    @classmethod
    def from_json(cls, data: dict[str, Any]) -> Metric:
        """A metric from to_json()'s JSON."""
        return cls(data["unit"], data["lower_is_better"], data["samples"])


#: Each row's metrics, by name, like {"throughput": {"W-54": Metric(...)}}.
Rows = dict[str, dict[str, Metric]]


def mann_whitney_p(a: Sequence[float], b: Sequence[float]) -> float:
    """Mann-Whitney U's two-sided p for two samples coming from one population.

    Ranks both samples together, averaging tied ranks, and approximates U's
    distribution as normal, with the tie and continuity corrections. That
    matches SciPy's mannwhitneyu(method="asymptotic"), and is close to exact
    for samples of 20, as the scorecard takes. When every value is the same,
    p is 1: there's nothing to tell apart.
    """
    n1, n2 = len(a), len(b)
    values = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    rank_sum = 0.0
    ties = 0.0
    i = 0
    while i < len(values):
        j = i
        while j + 1 < len(values) and values[j + 1][0] == values[i][0]:
            j += 1
        rank = (i + j) / 2 + 1
        rank_sum += rank * sum(1 for k in range(i, j + 1) if values[k][1] == 0)
        tied = j - i + 1
        ties += tied**3 - tied
        i = j + 1
    u = rank_sum - n1 * (n1 + 1) / 2
    n = n1 + n2
    variance = n1 * n2 / 12 * ((n + 1) - ties / (n * (n - 1)))
    if variance <= 0:
        return 1.0
    z = max(abs(u - n1 * n2 / 2) - 0.5, 0) / math.sqrt(variance)
    return min(1.0, math.erfc(z / math.sqrt(2)))


@dataclasses.dataclass
class Change:
    """How a metric changed from an earlier run."""

    before: float | None
    after: float | None
    #: The difference, after - before, in the metric's unit for dB, % and
    #: counts, and as a percentage of before for the rest.
    difference: float | None
    #: Mann-Whitney U's p, when both runs repeated the measurement.
    p: float | None
    #: "better", "worse" or "same".
    verdict: str


#: Units whose differences are in the unit itself, not as a percentage.
ABSOLUTE_UNITS = ("dB", "%", "tests")


def compare_metric(row: str, before: Metric, after: Metric) -> Change:
    """How a metric changed, and whether it's a real change.

    A repeated measurement changed if Mann-Whitney U says so. One measured
    once is exact, or as good as: the test count and coverage may never drop,
    and the SNR at 10% PER may get up to PER_MAX_REGRESSION_DB worse, the guardrail's limit.
    """
    old, new = before.median, after.median
    if old is None or new is None:
        return Change(old, new, None, None, "same")
    if after.unit in ABSOLUTE_UNITS:
        difference = new - old
    else:
        difference = (new - old) / old * 100 if old else None
    worse_sign = 1 if after.lower_is_better else -1
    p = None
    if len(before.samples) > 1 and len(after.samples) > 1:
        p = mann_whitney_p(before.samples, after.samples)
        significant = p < SIGNIFICANCE
        if not significant or new == old:
            verdict = "same"
        else:
            verdict = "worse" if (new - old) * worse_sign > 0 else "better"
    elif row == "per":
        if new - old > PER_MAX_REGRESSION_DB:
            verdict = "worse"
        else:
            verdict = "better" if new < old else "same"
    elif new == old:
        verdict = "same"
    else:
        verdict = "worse" if (new - old) * worse_sign > 0 else "better"
    return Change(old, new, difference, p, verdict)


def compare(before: Rows, after: Rows) -> dict[str, dict[str, Change]]:
    """Every metric both runs measured, and how it changed."""
    return {
        row: {
            name: compare_metric(row, before[row][name], metric)
            for name, metric in metrics.items()
            if name in before.get(row, {})
        }
        for row, metrics in after.items()
    }


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


def measure_tests() -> dict[str, Metric]:
    """Runs the C++ tests with coverage, which needs Bazel."""
    start = datetime.datetime.now().timestamp()
    subprocess.run(
        ["bazel", "run", "//:coverage"],
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
        "tests": Metric("tests", False, [count_tests(reports)]),
        "line coverage (%)": Metric("%", False, [100 * covered / total]),
    }


# Throughput and each block's cost ------------------------------------------


def bench_rows(results: dict[str, Any]) -> Rows:
    """Throughput and each block's cost, from fun-ofdm-bench's JSON.

    Throughput is receiver_chain's MS/s on each workload, and each block's
    cost is its ns per sample, for each repetition.
    """
    rows: Rows = {}
    for result in results["benchmarks"]:
        if result.get("run_type") != "iteration":
            continue
        block, workload = result["run_name"].split("/")[:2]
        if block == "receiver_chain":
            metric = rows.setdefault("throughput", {}).setdefault(
                workload, Metric("MS/s", False, [])
            )
            metric.samples.append(result["samples_per_second"] / 1e6)
        else:
            metric = rows.setdefault("block-cost", {}).setdefault(
                f"{block} {workload}", Metric("ns/sample", True, [])
            )
            metric.samples.append(result["time_per_sample"] * 1e9)
    return in_order(rows)


def in_order(rows: Rows) -> Rows:
    """The rows, with the benchmarks' metrics in a fixed order.

    The benchmarks run in a random order, interleaved, so throughput and
    block cost are put in the workloads' and blocks' order.
    """
    ordered = dict(rows)
    if "throughput" in rows:
        ordered["throughput"] = {
            name: rows["throughput"][name]
            for name in WORKLOADS
            if name in rows["throughput"]
        }
    if "block-cost" in rows:
        ordered["block-cost"] = {
            name: rows["block-cost"][name]
            for block in BLOCKS
            for workload in WORKLOADS
            if (name := f"{block} {workload}") in rows["block-cost"]
        }
    return ordered


def bench_filter(rows: Iterable[str]) -> str:
    """fun-ofdm-bench's --benchmark_filter for throughput, block cost or both."""
    wanted = set(rows)
    if wanted >= {"throughput", "block-cost"}:
        return "."
    return "^receiver_chain/" if "throughput" in wanted else "-^receiver_chain/"


def measure_bench(
    bench: pathlib.Path,
    rows: Iterable[str],
    repetitions: int,
    workload_seconds: float | None,
) -> Rows:
    """Measures throughput and each block's cost with fun-ofdm-bench."""
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "bench.json"
        return bench_rows(
            run_bench(bench, rows, repetitions, workload_seconds, out)
        )


def run_bench(
    bench: pathlib.Path,
    rows: Iterable[str],
    repetitions: int,
    workload_seconds: float | None,
    out: pathlib.Path,
) -> dict[str, Any]:
    """Runs fun-ofdm-bench, and returns its JSON, which it writes to out."""
    command = [
        str(bench),
        f"--benchmark_filter={bench_filter(rows)}",
        f"--benchmark_repetitions={repetitions}",
        # Each repetition at a different time, interleaved with the other
        # benchmarks', so a slow minute doesn't land on one benchmark.
        "--benchmark_enable_random_interleaving=true",
        f"--benchmark_out={out}",
        "--benchmark_out_format=json",
    ]
    if workload_seconds is not None:
        command.append(f"--workload_seconds={workload_seconds}")
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    return json.loads(out.read_text())


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


def measure_sim(
    fun_ofdm: pathlib.Path,
    repetitions: int,
    progress: Callable[[str], None],
) -> Rows:
    """Measures latency and jitter: runs each of SIM_RUNS repetitions times."""
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
    return sim_rows(runs)


# SNR at 10% PER -------------------------------------------------------------


def per_rows(curves: dict[str, list[sweep.Point]]) -> dict[str, Metric]:
    """Each rate's SNR at 10% PER, in dB, from the guardrail's sweep.

    The sweep's seeds are fixed, so it's measured once. A rate that never
    reaches 10% PER has no sample.
    """
    rows = {}
    for rate, points in curves.items():
        snr = sweep.snr_at_per(points, sweep.TARGET_PER)
        rows[rate] = Metric("dB", True, [] if snr is None else [snr])
    return rows


def measure_per(fun_ofdm: pathlib.Path, jobs: int) -> dict[str, Metric]:
    """Runs the guardrail's PER sweep, as tests/test_per.py runs it."""
    return per_rows(
        sweep.sweep(
            sweep.sim_runner(fun_ofdm, sweep.GUARDRAIL_SEED),
            sweep.RATES,
            sweep.GUARDRAIL_SNRS,
            sweep.GUARDRAIL_FRAMES,
            jobs,
        )
    )


# Output --------------------------------------------------------------------


def host_name(host: hostcheck.Host) -> str:
    """A directory name for the machine, from its board or CPU.

    "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz" becomes "intel-core-i7-10700k",
    and "Raspberry Pi 5 Model B Rev 1.0" becomes "raspberry-pi-5-model-b".
    """
    name = host.board or host.cpu_model or host.arch
    name = re.sub(r"\((R|TM)\)", "", name)
    name = re.sub(r"( CPU)? @ .*$| Rev .*$", "", name)
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def number(value: float | None) -> str:
    """A value to about 3 significant figures, or "—" for none."""
    if value is None:
        return "—"
    if abs(value) >= 100:
        return f"{value:.0f}"
    if abs(value) >= 10:
        return f"{value:.1f}"
    return f"{value:.2f}"


def summary(metric: Metric) -> str:
    """A metric's median, and its interquartile range if it was repeated."""
    quartiles = metric.quartiles
    if quartiles is None:
        return "not reached" if metric.unit == "dB" else "—"
    if len(metric.samples) == 1:
        return number(metric.median)
    return (
        f"{number(metric.median)} "
        f"({number(quartiles[0])} to {number(quartiles[1])})"
    )


def change_text(change: Change, unit: str) -> str:
    """A change's difference, like "+1.2%" or "-0.30 dB"."""
    if change.difference is None:
        return "—"
    suffix = f" {unit}" if unit in ("dB", "tests") else "%"
    if unit == "%":
        suffix = " points"
    sign = "+" if change.difference >= 0 else "-"
    return f"{sign}{number(abs(change.difference))}{suffix}"


def grid_cell(row: str, name: str) -> tuple[str, str]:
    """Where a metric goes in its row's grid: its line and its column.

    Block cost has a line per block and a column per workload, latency a line
    per run and a column per percentile, and jitter a line per run and block
    and a column per measure. The rest have one column.
    """
    if row == "block-cost":
        block, workload = name.split(" ")
        return block, workload
    if row == "latency":
        run, percentile = name.split(" ", 1)
        return run, percentile
    if row == "jitter":
        run, block, measure = name.split(" ", 2)
        return f"{run} {block}", measure
    return name, "Value"


def grid(row: str, metrics: dict[str, Metric]) -> list[str]:
    """A row's metrics as a Markdown table, in its grid (grid_cell)."""
    lines: dict[str, dict[str, str]] = {}
    columns: dict[str, None] = {}
    for name, metric in metrics.items():
        line, column = grid_cell(row, name)
        lines.setdefault(line, {})[column] = summary(metric)
        columns[column] = None
    table = [
        "| | " + " | ".join(columns) + " |",
        "|---" * (len(columns) + 1) + "|",
    ]
    for line, cells in lines.items():
        values = " | ".join(cells.get(column, "") for column in columns)
        table.append(f"| {line} | {values} |")
    return table


def markdown(
    meta: dict[str, Any],
    host: hostcheck.Host,
    rows: Rows,
    changes: dict[str, dict[str, Change]] | None,
) -> str:
    """The scorecard as Markdown: the machine, then a table per row."""
    lines = [
        f"# Scorecard: {meta['host']}, {host.configuration}",
        "",
        (
            f"{meta['date']}, fun-ofdm {host.fun_ofdm_version or 'unknown'} "
            f"at {(host.commit or 'an unknown commit')[:12]}"
            f"{' (with uncommitted changes)' if host.dirty else ''}, built "
            f"with {host.compiler or 'an unknown compiler'}. Each repeated "
            f"metric is the median of {meta['repetitions']} runs, with the "
            "interquartile range in brackets."
        ),
        "",
        "```text",
        hostcheck.report(host),
        "```",
    ]
    if changes is not None:
        lines += [
            "",
            (
                f"Compared with {meta['compared_with']}: a repeated metric "
                f"changed if Mann-Whitney U's p is below {SIGNIFICANCE}."
            ),
        ]
    for row, metrics in rows.items():
        units = {metric.unit for metric in metrics.values()}
        unit = f" ({units.pop()})" if len(units) == 1 else ""
        lines += ["", f"## {TITLES[row]}{unit}", ""]
        if changes is None:
            lines += grid(row, metrics)
        else:
            lines += [
                "| Metric | Before | After | Change | p | |",
                "|---|---|---|---|---|---|",
            ]
            for name, metric in metrics.items():
                change = changes.get(row, {}).get(name)
                if change is None:
                    lines.append(f"| {name} | — | {summary(metric)} | | | |")
                    continue
                p = "" if change.p is None else f"{change.p:.3f}"
                verdict = "" if change.verdict == "same" else change.verdict
                lines.append(
                    f"| {name} | {number(change.before)} | "
                    f"{summary(metric)} | {change_text(change, metric.unit)} "
                    f"| {p} | {verdict} |"
                )
    return "\n".join(lines) + "\n"


def to_json(
    meta: dict[str, Any],
    host: hostcheck.Host,
    rows: Rows,
    changes: dict[str, dict[str, Change]] | None,
) -> dict[str, Any]:
    """The scorecard as JSON, with every repetition's value."""
    result = {
        **meta,
        "hostcheck": hostcheck.to_json(host),
        "rows": {
            row: {name: metric.to_json() for name, metric in metrics.items()}
            for row, metrics in rows.items()
        },
    }
    if changes is not None:
        result["changes"] = {
            row: {name: dataclasses.asdict(c) for name, c in found.items()}
            for row, found in changes.items()
        }
    return result


def rows_from_json(data: dict[str, Any]) -> Rows:
    """The rows of a scorecard's JSON."""
    return in_order(
        {
            row: {name: Metric.from_json(m) for name, m in metrics.items()}
            for row, metrics in data["rows"].items()
        }
    )


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
    if input("\nStart the scorecard? [Y/n] ").strip().lower() == "n":
        sys.exit(1)
    return cpus


# Main ----------------------------------------------------------------------


def main(argv: Sequence[str] | None = None) -> int:
    """Runs the scorecard from the command line."""
    bin_dir = hostcheck.DEFAULT_FUN_OFDM.parent
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--milestone",
        required=True,
        help="the results' directory under docs/perf/, like milestone2",
    )
    parser.add_argument(
        "--host",
        help="the machine's directory (default: from its board or CPU)",
    )
    parser.add_argument(
        "--cpus",
        help="pin everything to these CPUs, like 1-15 (the Pinned "
        "configuration needs it)",
    )
    parser.add_argument(
        "--rows",
        nargs="+",
        choices=ROWS,
        default=list(ROWS),
        help="the rows to measure",
    )
    parser.add_argument("--repetitions", type=int, default=DEFAULT_REPETITIONS)
    parser.add_argument(
        "--workload-seconds",
        type=float,
        help="shorter workloads for a quick try (default: the standard 10 s)",
    )
    parser.add_argument(
        "--compare",
        type=pathlib.Path,
        help="an earlier scorecard's JSON to compare with",
    )
    parser.add_argument(
        "-i",
        "--interactive",
        action="store_true",
        help="walk through preparing the machine first",
    )
    parser.add_argument("--bin-dir", type=pathlib.Path, default=bin_dir)
    parser.add_argument(
        "--out-dir", type=pathlib.Path, default=REPO / "docs" / "perf"
    )
    args = parser.parse_args(argv)

    fun_ofdm = args.bin_dir / "fun-ofdm"
    bench = args.bin_dir / "fun-ofdm-bench"
    for binary in (fun_ofdm, bench):
        if not binary.exists():
            parser.error(f"{binary} doesn't exist: bazel run install")
    if args.repetitions < 1:
        parser.error("--repetitions must be at least 1")
    before = (
        rows_from_json(json.loads(args.compare.read_text()))
        if args.compare
        else None
    )

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

    out_dir = args.out_dir / args.milestone / (args.host or host_name(host))
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = out_dir / host.configuration.lower()

    rows: Rows = {}
    if "tests" in args.rows:
        if shutil.which("bazel") is None:
            progress("Tests skipped: they need Bazel")
        else:
            progress("The tests, with coverage")
            rows["tests"] = measure_tests()
    if {"throughput", "block-cost"} & set(args.rows):
        progress("fun-ofdm-bench, about 35 minutes at 20 repetitions")
        rows.update(
            measure_bench(
                bench,
                [r for r in args.rows if r in ("throughput", "block-cost")],
                args.repetitions,
                args.workload_seconds,
            )
        )
    if {"latency", "jitter"} & set(args.rows):
        found = measure_sim(fun_ofdm, args.repetitions, progress)
        rows.update(
            {r: found[r] for r in ("latency", "jitter") if r in args.rows}
        )
    if "per" in args.rows:
        progress("The PER sweep")
        rows["per"] = measure_per(
            fun_ofdm, max(1, len(os.sched_getaffinity(0)) // 2)
        )
    rows = {row: rows[row] for row in ROWS if row in rows}

    meta = {
        "milestone": args.milestone,
        "host": args.host or host_name(host),
        "configuration": host.configuration,
        "date": datetime.datetime.now(datetime.UTC).isoformat(
            timespec="seconds"
        ),
        "repetitions": args.repetitions,
        "workload_seconds": args.workload_seconds,
        "cpus": cpus,
        "compared_with": str(args.compare) if args.compare else None,
    }
    changes = compare(before, rows) if before is not None else None
    stem.with_suffix(".json").write_text(
        json.dumps(to_json(meta, host, rows, changes), indent=2) + "\n"
    )
    table = markdown(meta, host, rows, changes)
    stem.with_suffix(".md").write_text(table)
    print(table)
    progress(f"wrote {stem}.json and {stem}.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
