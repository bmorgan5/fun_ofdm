"""Draws the charts from a directory of results, like scorecard.py reads.

    uv run tools/scorecard/charts.py out/results/v2.1.0

It writes PNGs to DIR/charts/, and DIR/charts/charts.md, which lists them and
checks that the histograms agree with the traces. Each chart is drawn if its
results are there (README.md):

- per.png: PER vs SNR at every rate, on hardware and in simulation.
- block-times-<run>.png: each block's processing time at each percentile,
  from measure.py latency's histograms.
- block-jitter-<run>.png: each block's start interval and wait, likewise.
- realtime.png: overflows, latency p99 and the busiest CPU against
  bandwidth, from hardware_sweep.py realtime.
- timeline-<trace>.png: each block's runs, a row per thread, over a short
  window of a trace that hardware_sweep.py realtime saved.
"""

import argparse
import json
import math
import pathlib
import statistics
import sys
from collections.abc import Callable, Sequence
from typing import Any

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
from matplotlib.axes import Axes
from matplotlib.figure import Figure

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "results"))

sys.path.insert(0, str(REPO / "tools" / "hardware"))

import hardware
import result
import scorecard

#: The percentiles of the block-times charts, as fractions.
PERCENTILES = (0.5, 0.9, 0.99, 0.999, 0.9999)

#: How long a window of a trace the timelines show, in ms.
TIMELINE_MS = 20.0

#: The percentiles the metrics-agree table compares.
AGREE_PERCENTILES = (50, 99)

#: How far a histogram's percentile may be from its trace's for the two to
#: agree, in %: the histograms keep 3 significant digits, which is 0.1%.
AGREE_TOLERANCE_PERCENT = 0.2

#: A slice of a trace: its thread's name, the slice's name, and its start
#: and duration in ns.
Slice = tuple[str, str, int, int]


def find(directory: pathlib.Path, tool: str) -> tuple[pathlib.Path, Any] | None:
    """The result in one of the directory's subdirectories made by a tool.

    Like scorecard.py, only the directory's own subdirectories are read, and
    a partial result, from an interrupted sweep, is refused.

    Raises:
        ValueError: The result is a partial one.
    """
    for path in sorted(directory.glob("*/result.json")):
        data = json.loads(path.read_text())
        if data["provenance"]["tool"].startswith(tool):
            if data.get("complete") is False:
                raise ValueError(f"{path}: the sweep was interrupted")
            return path.parent, data
    return None


def save(figure: Figure, path: pathlib.Path) -> pathlib.Path:
    """Saves a figure as a PNG, and closes it."""
    figure.tight_layout()
    figure.savefig(path, dpi=120)
    plt.close(figure)
    return path


# PER vs SNR ------------------------------------------------------------------


def per_chart(
    sim: dict[str, Any] | None,
    hardware: dict[str, Any] | None,
    out: pathlib.Path,
) -> pathlib.Path | None:
    """PER vs SNR, a color per rate: hardware solid, simulation dashed.

    A step with no losses is drawn at a tenth of a frame lost, as
    tools/per/sweep.py's snr_at_per() counts it, so it stays on the log
    scale.
    """
    if sim is None and hardware is None:
        return None
    figure, axes = plt.subplots(figsize=(9, 6))
    rates = list((hardware or sim or {})["rates"])
    colors = plt.colormaps["tab20"]
    for i, rate in enumerate(rates):
        color = colors(i / max(1, len(rates) - 1))
        if hardware is not None and rate in hardware["rates"]:
            steps = [
                s
                for s in hardware["rates"][rate]["steps"]
                if s["snr_db"] is not None and s["status"] == "finished"
            ]
            axes.plot(
                [s["snr_db"] for s in steps],
                [max(s["per"], 0.1 / max(s["sent"], 1)) for s in steps],
                "-o",
                color=color,
                markersize=3,
                label=rate,
            )
        if sim is not None and rate in sim["rates"]:
            points = sim["rates"][rate]["points"]
            axes.plot(
                [p["snr_db"] for p in points],
                [max(p["per"], 0.1 / p["sent"]) for p in points],
                "--",
                color=color,
                label=None if hardware is not None else rate,
            )
    axes.axhline(0.1, color="gray", linewidth=0.5)
    axes.set_yscale("log")
    axes.set_xlabel("SNR (dB)")
    axes.set_ylabel("PER")
    title = "PER vs SNR"
    if sim is not None and hardware is not None:
        title += ": on a B210 (solid) and in simulation (dashed)"
    axes.set_title(title)
    axes.grid(True, which="both", alpha=0.3)
    axes.legend(fontsize="small", ncol=2)
    return save(figure, out / "per.png")


# Each block's times ----------------------------------------------------------


def percentile(
    buckets: Sequence[Sequence[int]], fraction: float
) -> float | None:
    """The value below which a fraction of a histogram's values fall.

    Buckets are [value, count] pairs, each value the largest its bucket
    holds, as in --json's histograms.
    """
    total = sum(count for _, count in buckets)
    if not total:
        return None
    rank = hdr_rank(fraction, total)
    seen = 0
    for value, count in buckets:
        seen += count
        if seen >= rank:
            return value
    return buckets[-1][0]


def hdr_rank(fraction: float, count: int) -> int:
    """The rank of a percentile among count values, counting from 1.

    HdrHistogram rounds to the nearest rank, as hdr_value_at_percentile()
    does, so the percentiles here match --json's.
    """
    return max(1, int(fraction * count + 0.5))


def nines(fraction: float) -> float:
    """A percentile on a scale where each 9 is a step: 0.9 → 1, 0.99 → 2."""
    return -math.log10(1 - fraction)


def block_times_chart(
    run: str, histograms: dict[str, Any], out: pathlib.Path
) -> pathlib.Path:
    """Each block's processing time at p50, p90, p99, p99.9, p99.99 and max."""
    figure, axes = plt.subplots(figsize=(8, 5))
    ticks = [nines(p) for p in PERCENTILES] + [nines(PERCENTILES[-1]) + 1]
    labels = [f"p{100 * p:g}" for p in PERCENTILES] + ["max"]
    for block in scorecard.BLOCKS:
        buckets = histograms["blocks"].get(block, {}).get("processing", [])
        if not buckets:
            continue
        values = [percentile(buckets, p) for p in PERCENTILES]
        values.append(buckets[-1][0])
        axes.plot(
            ticks,
            [math.nan if v is None else v / 1000 for v in values],
            "-o",
            label=block,
        )
    axes.set_xticks(ticks, labels)
    axes.set_yscale("log")
    axes.set_ylabel("Processing time (µs)")
    axes.set_title(f"Each block's processing time, {run}")
    axes.grid(True, which="both", alpha=0.3)
    axes.legend(fontsize="small")
    return save(figure, out / f"block-times-{run}.png")


def cdf(axes: Axes, buckets: Sequence[Sequence[int]], label: str) -> None:
    """A histogram's cumulative distribution, in µs."""
    total = sum(count for _, count in buckets)
    if not total:
        return
    seen = 0
    xs, ys = [], []
    for value, count in buckets:
        seen += count
        xs.append(max(value, 1) / 1000)
        ys.append(seen / total)
    axes.step(xs, ys, where="post", label=label)


def block_jitter_chart(
    run: str, histograms: dict[str, Any], out: pathlib.Path
) -> pathlib.Path:
    """Each block's start interval and wait to start, as distributions."""
    figure, (intervals, waits) = plt.subplots(1, 2, figsize=(12, 5))
    for block in scorecard.BLOCKS:
        times = histograms["blocks"].get(block, {})
        cdf(intervals, times.get("start_interval", []), block)
        cdf(waits, times.get("wait", []), block)
    for axes, what in (
        (intervals, "Start interval: one start to the next"),
        (waits, "Wait: input ready to the start"),
    ):
        axes.set_xscale("log")
        axes.set_xlabel("µs")
        axes.set_ylabel("Fraction of runs")
        axes.set_title(f"{what}, {run}")
        axes.grid(True, which="both", alpha=0.3)
    intervals.legend(fontsize="small")
    return save(figure, out / f"block-jitter-{run}.png")


# Real time -------------------------------------------------------------------


def values_at(
    steps: Sequence[dict[str, Any]],
    bandwidth: float,
    value: Callable[[dict[str, Any]], Any],
) -> list[Any]:
    """A value of each step at a bandwidth, leaving out the steps without it."""
    return [
        v
        for s in steps
        if s["bandwidth_mhz"] == bandwidth
        for v in [value(s)]
        if v is not None
    ]


def median_or_nan(values: Sequence[float]) -> float:
    """The median, or NaN for none, which matplotlib leaves a gap for."""
    return statistics.median(values) if values else math.nan


def max_or_nan(values: Sequence[float]) -> float:
    """The largest, or NaN for none, which matplotlib leaves a gap for."""
    return max(values) if values else math.nan


def realtime_chart(realtime: dict[str, Any], out: pathlib.Path) -> pathlib.Path:
    """Overflows, latency p99, and each CPU's use, against bandwidth.

    Each point is the median of its runs, but overflows are the most of any.
    The top row has overflows and latency, and the bottom a chart per
    workload, with a line per CPU.
    """
    steps = realtime["steps"]
    workloads = list(dict.fromkeys(s["workload"] for s in steps))
    figure, grid = plt.subplots(
        2, max(2, len(workloads)), figsize=(15, 9), squeeze=False
    )
    overflows, latency = grid[0][0], grid[0][1]
    for axes in grid[0][2:]:
        axes.set_visible(False)
    for column, workload in enumerate(workloads):
        mine = [s for s in steps if s["workload"] == workload]
        bandwidths = sorted({s["bandwidth_mhz"] for s in mine})
        overflows.plot(
            bandwidths,
            [
                max_or_nan(values_at(mine, b, lambda s: s.get("overflows")))
                for b in bandwidths
            ],
            "-o",
            label=workload,
        )
        if workload != "idle":
            latency.plot(
                bandwidths,
                [
                    median_or_nan(
                        values_at(
                            mine,
                            b,
                            lambda s: s.get("latency_us", {}).get("p99"),
                        )
                    )
                    / 1000
                    for b in bandwidths
                ],
                "-o",
                label=workload,
            )
        cpu = grid[1][column]
        cores = sorted(
            {c for s in mine for c in s["cpu_percent"]},
            key=lambda c: int(c.removeprefix("cpu") or 0),
        )
        for core in cores:
            cpu.plot(
                bandwidths,
                [
                    median_or_nan(
                        values_at(
                            mine,
                            b,
                            lambda s, core=core: s["cpu_percent"].get(core),
                        )
                    )
                    for b in bandwidths
                ],
                "-o",
                label=core,
            )
        cpu.set_title(f"Each CPU's use, {workload}")
        cpu.set_ylabel("CPU use (%)")
        cpu.set_ylim(0, 105)
    for axes in grid[1][len(workloads) :]:
        axes.set_visible(False)
    overflows.set_ylabel("Overflows (most in any run)")
    overflows.set_yscale("symlog")
    latency.set_ylabel("Latency p99 (ms)")
    for axes in [overflows, latency, *grid[1][: len(workloads)]]:
        axes.set_xlabel("Bandwidth, and sample rate (MHz)")
        axes.grid(True, alpha=0.3)
        axes.legend(fontsize="small")
    figure.suptitle("Keeping up on a B210")
    return save(figure, out / "realtime.png")


# Traces ----------------------------------------------------------------------


def read_slices(trace: pathlib.Path) -> list[Slice]:
    """Each block's slices in a trace, with Perfetto's trace processor."""
    from perfetto.trace_processor import TraceProcessor

    processor = TraceProcessor(trace=str(trace))
    try:
        rows = processor.query(
            "select t.name as thread, s.name as name, s.ts as ts, s.dur as dur "
            "from slice s join thread_track tt on s.track_id = tt.id "
            "join thread t using (utid) where s.dur > 0 order by s.ts"
        )
        return [(r.thread, r.name, r.ts, r.dur) for r in rows]
    finally:
        processor.close()


def timeline_chart(
    name: str, slices: Sequence[Slice], out: pathlib.Path
) -> pathlib.Path | None:
    """Each block's runs over TIMELINE_MS from the middle of a trace."""
    if not slices:
        return None
    start = slices[len(slices) // 2][2]
    end = start + int(TIMELINE_MS * 1e6)
    threads = [b for b in scorecard.BLOCKS if any(s[0] == b for s in slices)]
    figure, axes = plt.subplots(figsize=(12, 0.5 * len(threads) + 1.5))
    for row, thread in enumerate(threads):
        bars = [
            ((ts - start) / 1e6, dur / 1e6)
            for t, _, ts, dur in slices
            if t == thread and ts < end and ts + dur > start
        ]
        axes.broken_barh(bars, (row - 0.4, 0.8))
    axes.set_yticks(range(len(threads)), threads)
    axes.invert_yaxis()
    axes.set_xlim(0, TIMELINE_MS)
    axes.set_xlabel("ms")
    axes.set_title(f"Each block's runs, {name}")
    axes.grid(True, axis="x", alpha=0.3)
    return save(figure, out / f"timeline-{name}.png")


def block_durations(slices: Sequence[Slice]) -> dict[str, list[int]]:
    """Each block's processing times in a trace, sorted, in ns."""
    durations: dict[str, list[int]] = {}
    for thread, name, _, dur in slices:
        if thread == name:
            durations.setdefault(name, []).append(dur)
    for values in durations.values():
        values.sort()
    return durations


def nearest_rank(values: Sequence[int], p: float) -> int:
    """A sorted list's percentile, rounded to a rank as HdrHistogram does."""
    return values[min(len(values), hdr_rank(p / 100, len(values))) - 1]


def agreement_rows(
    name: str, summary: dict[str, Any], slices: Sequence[Slice]
) -> list[dict[str, str]]:
    """Each block's p50 and p99 from the histograms and from the trace.

    The histograms keep 3 significant digits, so they agree to about 0.1%,
    if the trace has every run the histogram counted. A trace that couldn't
    keep up with the receiver, or a run longer than the trace, has fewer:
    the Slices column says how many.
    """
    durations = block_durations(slices)
    rows = []
    for block in scorecard.BLOCKS:
        histogram = (
            summary["metrics"]["blocks"].get(block, {}).get("processing")
        )
        if histogram is None or block not in durations:
            continue
        found, counted = len(durations[block]), histogram["count"]
        for p in AGREE_PERCENTILES:
            key = {50: "p50", 99: "p99"}[p]
            mine, theirs = histogram[key], nearest_rank(durations[block], p)
            difference = (mine - theirs) / theirs * 100 if theirs else math.nan
            rows.append(
                {
                    "Run": name,
                    "Block": block,
                    "Slices": "all"
                    if found == counted
                    else f"{found} of {counted}",
                    "Percentile": key,
                    "Histogram (µs)": f"{mine / 1000:.1f}",
                    "Trace (µs)": f"{theirs / 1000:.1f}",
                    "Difference": f"{difference:+.2f}%",
                }
            )
    return rows


def timeline_traces(
    realtime_dir: pathlib.Path, realtime: dict[str, Any]
) -> list[str]:
    """The traces to draw: two for each workload.

    Its trace at its highest bandwidth kept up with, and at the next one up,
    where it fell behind.
    """
    names = []
    bandwidths = sorted({s["bandwidth_mhz"] for s in realtime["steps"]})
    for workload, highest in realtime["highest_kept_up_mhz"].items():
        picks = []
        if highest is not None:
            picks.append(highest)
        above = [b for b in bandwidths if highest is None or b > highest]
        if above:
            picks.append(above[0])
        names += [hardware.trace_name(workload, b) for b in picks]
    return [
        n
        for n in names
        if (realtime_dir / "traces" / f"{n}.pftrace.gz").exists()
    ]


# Main ------------------------------------------------------------------------


def draw(
    directory: pathlib.Path,
    slices: Callable[[pathlib.Path], list[Slice]] = read_slices,
) -> tuple[list[pathlib.Path], list[dict[str, str]]]:
    """Draws every chart its results allow, to directory/charts/.

    Returns:
        The charts, and the metrics-agree table's rows.
    """
    out = directory / "charts"
    out.mkdir(exist_ok=True)
    # Only this run's charts, which scorecard.py links
    for old in out.glob("*.png"):
        old.unlink()
    charts: list[pathlib.Path | None] = []
    sim = find(directory, "tools/per/sweep.py")
    hardware = find(directory, "tools/hardware/hardware_sweep.py per")
    charts.append(
        per_chart(
            sim[1] if sim else None, hardware[1] if hardware else None, out
        )
    )
    agreement: list[dict[str, str]] = []
    latency = find(directory, "tools/scorecard/measure.py latency")
    if latency is not None:
        for run, histograms in latency[1].get("histograms", {}).items():
            charts.append(block_times_chart(run, histograms, out))
            charts.append(block_jitter_chart(run, histograms, out))
        for trace in sorted((latency[0] / "traces").glob("*.pftrace.gz")):
            name = trace.name.removesuffix(".pftrace.gz")
            summary = trace.with_name(f"{name}.json")
            if summary.exists():
                agreement += agreement_rows(
                    f"sim {name}",
                    json.loads(summary.read_text()),
                    slices(trace),
                )
    realtime = find(directory, "tools/hardware/hardware_sweep.py realtime")
    if realtime is not None:
        realtime_dir, data = realtime
        charts.append(realtime_chart(data, out))
        drawn = timeline_traces(realtime_dir, data)
        for trace in sorted((realtime_dir / "traces").glob("*.pftrace.gz")):
            name = trace.name.removesuffix(".pftrace.gz")
            found = slices(trace)
            if name in drawn:
                charts.append(timeline_chart(name, found, out))
            summary = trace.with_name(f"{name}.json")
            if summary.exists():
                agreement += agreement_rows(
                    name, json.loads(summary.read_text()), found
                )
    return [c for c in charts if c is not None], agreement


def agree_text(agreement: Sequence[dict[str, str]]) -> str:
    """Whether the histograms agree with the traces that have every slice."""
    complete = [r for r in agreement if r["Slices"] == "all"]
    intro = (
        "Each traced run's processing times, from its --json histograms and "
        "from its trace's slices, for the same run."
    )
    if not complete:
        return f"{intro} No trace has every slice, so nothing is checked."
    worst = max(abs(float(r["Difference"].rstrip("%"))) for r in complete)
    verdict = "Pass" if worst <= AGREE_TOLERANCE_PERCENT else "Fail"
    return (
        f"{intro} **{verdict}:** where the trace has every slice, the largest "
        f"difference is {worst:.2f}%, against {AGREE_TOLERANCE_PERCENT}% "
        "allowed."
    )


def markdown(
    charts: Sequence[pathlib.Path], agreement: list[dict[str, str]]
) -> str:
    """charts.md: each chart, then the metrics-agree table."""
    lines = ["# Charts", ""]
    lines += [f"![{c.stem}]({c.name})" for c in charts]
    if agreement:
        lines += [
            "",
            "## The histograms against the traces",
            "",
            agree_text(agreement),
            "",
        ]
        lines.append(result.markdown_table(agreement).rstrip())
    return "\n".join(lines) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    """Draws the charts from the command line."""
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "dir", type=pathlib.Path, help="a directory of result directories"
    )
    args = parser.parse_args(argv)
    if not args.dir.is_dir():
        parser.error(f"{args.dir} isn't a directory")
    charts, agreement = draw(args.dir)
    (args.dir / "charts" / "charts.md").write_text(markdown(charts, agreement))
    for chart in charts:
        print(chart)
    return 0


if __name__ == "__main__":
    sys.exit(main())
