"""The scorecard: each measurement's headline numbers, in one place.

It reads a directory of results, each in a directory of its own, as the
measurement tools write them with --out (tools/results/result.py), and writes
the scorecard to the directory, as scorecard.md and scorecard.json:

    uv run tools/scorecard/scorecard.py out/results/v2.1.0 --compare OLD

Each metric has its median and interquartile range. With --compare, the
table also has each metric's change from an earlier directory, and whether
it's significant. It runs nothing itself: measure.py, the sweeps and the
hardware tests measure (README.md).
"""

import argparse
import dataclasses
import json
import math
import os
import pathlib
import statistics
import sys
from collections.abc import Callable, Sequence
from typing import Any

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "results"))

import result

#: The rows the scorecard has, in order, by the names the JSON uses.
ROWS = (
    "tests",
    "throughput",
    "block-cost",
    "latency",
    "jitter",
    "per",
    "realtime",
    "per-hardware",
    "link",
)

TITLES = {
    "tests": "Unit tests and line coverage of src/",
    "throughput": "Receiver throughput",
    "block-cost": "Each block's cost, on its own",
    "latency": "Latency, last sample of a frame in to its payload out",
    "jitter": "Jitter of each block",
    "per": "SNR at 10% PER, in simulation",
    "realtime": "Highest bandwidth kept up with, on a B210",
    "per-hardware": "SNR at 10% PER on a B210, and the loss against simulation",
    "link": "Frames received intact in the link smoke test",
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

#: A difference is significant when Mann-Whitney U's p is below this.
SIGNIFICANCE = 0.05

#: How much a metric measured once may change before the comparison calls it
#: better or worse, in its unit. The SNR at 10% PER may get as much worse as
#: tests/test_per.py's guardrail allows. On hardware, a step is 1 dB, and a
#: run's losses vary from run to run.
TOLERANCES = {"per": 0.25, "per-hardware": 1.0, "link": 1.0}


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
ABSOLUTE_UNITS = ("dB", "%", "tests", "MHz")


def compare_metric(row: str, before: Metric, after: Metric) -> Change:
    """How a metric changed, and whether it's a real change.

    A repeated measurement changed if Mann-Whitney U says so. One measured
    once is exact, like the test count and coverage, which may never drop,
    or changes only beyond its row's TOLERANCES.
    """
    old, new = before.median, after.median
    if old is None or new is None:
        # Measured once but not now, like a rate no longer reaching 10% PER,
        # is worse, and the other way better.
        if old is None and new is None:
            verdict = "same"
        else:
            verdict = "worse" if new is None else "better"
        return Change(old, new, None, None, verdict)
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
    elif abs(new - old) <= TOLERANCES.get(row, 0):
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


# Reading the results --------------------------------------------------------


def measured_rows(data: dict[str, Any]) -> Rows:
    """The rows a measure.py result holds, with every repetition."""
    return {
        row: {name: Metric.from_json(m) for name, m in metrics.items()}
        for row, metrics in data["rows"].items()
    }


def per_rows(data: dict[str, Any]) -> Rows:
    """Each rate's SNR at 10% PER, from a tools/per/sweep.py result.

    The sweep's seeds are fixed, so it's measured once. A rate that never
    reaches 10% PER has no sample.
    """
    return {
        "per": {
            rate: Metric("dB", True, [] if snr is None else [snr])
            for rate, curve in data["rates"].items()
            for snr in [curve["snr_at_target_per"]]
        }
    }


def realtime_rows(data: dict[str, Any]) -> Rows:
    """Each workload's highest bandwidth kept up with, from hardware_sweep.py."""
    return {
        "realtime": {
            workload: Metric("MHz", False, [] if mhz is None else [mhz])
            for workload, mhz in data["highest_kept_up_mhz"].items()
        }
    }


def per_hardware_rows(data: dict[str, Any]) -> Rows:
    """Each rate's SNR at 10% PER on hardware, and its implementation loss."""
    metrics = {}
    for rate, curve in data["rates"].items():
        for name, key in (
            ("SNR", "snr_at_target_per"),
            ("loss", "implementation_loss_db"),
        ):
            value = curve[key]
            metrics[f"{rate} {name}"] = Metric(
                "dB", True, [] if value is None else [value]
            )
    return {"per-hardware": metrics}


def link_rows(data: dict[str, Any]) -> Rows:
    """Each rate's frames received intact in the link smoke test, in %."""
    metrics = {}
    for name, run in data["runs"].items():
        summary = run.get("summary")
        samples = []
        if summary is not None and summary["sent"]["packets"]:
            intact = summary["received"]["packets"]
            samples = [100 * intact / summary["sent"]["packets"]]
        metrics[name] = Metric("%", False, samples)
    return {"link": metrics}


#: How to read each tool's results, by the start of its provenance's tool.
READERS: dict[str, Callable[[dict[str, Any]], Rows]] = {
    "tools/scorecard/measure.py": measured_rows,
    "tools/per/sweep.py": per_rows,
    "tools/hardware/hardware_sweep.py realtime": realtime_rows,
    "tools/hardware/hardware_sweep.py per": per_hardware_rows,
    "hil/test_link_smoke.py": link_rows,
}


@dataclasses.dataclass
class Source:
    """A result the scorecard read: where it is, and what made it."""

    path: str
    provenance: dict[str, Any]


def read_results(directory: pathlib.Path) -> tuple[Rows, list[Source]]:
    """The rows of every result in a directory's subdirectories.

    Only the directory's own subdirectories are read, so another set of
    results inside it, like a repeat of some measurements, isn't mixed in.

    Raises:
        ValueError: Two results have the same row, a result's tool isn't one
            the scorecard reads, a result is a partial one that an
            interrupted sweep saved, or a result can't be read.
    """
    rows: Rows = {}
    sources = []
    for path in sorted(directory.glob("*/result.json")):
        try:
            data = json.loads(path.read_text())
            tool = data["provenance"]["tool"]
        except (ValueError, KeyError) as error:
            raise ValueError(f"{path}: not a result: {error!r}") from error
        if data.get("complete") is False:
            raise ValueError(f"{path}: the sweep was interrupted: run it again")
        reader = next(
            (r for prefix, r in READERS.items() if tool.startswith(prefix)),
            None,
        )
        if reader is None:
            raise ValueError(f"{path}: the scorecard doesn't read {tool}")
        try:
            found = reader(data)
        except (KeyError, TypeError) as error:
            raise ValueError(f"{path}: can't read {tool}: {error!r}") from error
        for row, metrics in found.items():
            if row in rows:
                raise ValueError(f"{path}: another result has {row} too")
            rows[row] = metrics
        sources.append(
            Source(str(path.parent.relative_to(directory)), data["provenance"])
        )
    return in_order({row: rows[row] for row in ROWS if row in rows}), sources


def consistency_notes(sources: Sequence[Source]) -> list[str]:
    """What sets the results apart, which a scorecard shouldn't hide.

    Each result should come from the same build of fun-ofdm, at the same
    commit, with no uncommitted changes: otherwise its rows aren't of the
    same code.
    """
    notes = []
    for name in ("fun-ofdm", "fun-ofdm-bench"):
        digests = {
            s.provenance["binaries"][name]["sha256"][:12]: s.path
            for s in sources
            if name in s.provenance.get("binaries", {})
        }
        if len(digests) > 1:
            notes.append(
                f"The results ran {len(digests)} different {name} binaries: "
                + ", ".join(f"{d} ({p}/)" for d, p in digests.items())
            )
    commits = {
        (s.provenance["host"].get("commit") or "unknown")[:12] for s in sources
    }
    if len(commits) > 1:
        notes.append(
            f"The results are from commits {', '.join(sorted(commits))}"
        )
    dirty = [s.path for s in sources if s.provenance["host"].get("dirty")]
    if dirty:
        notes.append(
            "These had uncommitted changes: "
            + ", ".join(f"{p}/" for p in dirty)
        )
    return notes


# Output --------------------------------------------------------------------


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
    suffix = f" {unit}" if unit in ABSOLUTE_UNITS else "%"
    if unit == "%":
        suffix = " points"
    sign = "+" if change.difference >= 0 else "-"
    return f"{sign}{number(abs(change.difference))}{suffix}"


def grid_cell(row: str, name: str) -> tuple[str, str]:
    """Where a metric goes in its row's grid: its line and its column.

    Block cost has a line per block and a column per workload, latency a line
    per run and a column per percentile, jitter a line per run and block and
    a column per measure, and PER on hardware a line per rate and a column
    for its SNR and its loss. The rest have one column.
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
    if row == "per-hardware":
        rate, measure = name.split(" ", 1)
        return rate, measure
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
    sources: Sequence[Source],
    rows: Rows,
    changes: dict[str, dict[str, Change]] | None,
    charts: Sequence[str] = (),
) -> str:
    """The scorecard as Markdown.

    What it's from, a table per row, then links to the charts (charts.py), by
    their paths from the scorecard.
    """
    lines = [
        f"# Scorecard: {meta['name']}",
        "",
        (
            "Each repeated metric is the median of its runs, with the "
            "interquartile range in brackets. It's from these results:"
        ),
        "",
    ]
    lines += [
        f"- `{source.path}/`: {result.describe(source.provenance)}"
        for source in sources
    ]
    notes = consistency_notes(sources)
    if notes:
        lines += ["", "**Check:**", ""] + [f"- {note}" for note in notes]
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
    if charts:
        lines += ["", "## Charts", ""]
        lines += [f"- [{pathlib.Path(c).stem}]({c})" for c in charts]
    return "\n".join(lines) + "\n"


def to_json(
    meta: dict[str, Any],
    sources: Sequence[Source],
    rows: Rows,
    changes: dict[str, dict[str, Change]] | None,
) -> dict[str, Any]:
    """The scorecard as JSON, with every repetition's value."""
    data = {
        **meta,
        "sources": [dataclasses.asdict(source) for source in sources],
        "rows": {
            row: {name: metric.to_json() for name, metric in metrics.items()}
            for row, metrics in rows.items()
        },
    }
    if changes is not None:
        data["changes"] = {
            row: {name: dataclasses.asdict(c) for name, c in found.items()}
            for row, found in changes.items()
        }
    return data


def main(argv: Sequence[str] | None = None) -> int:
    """Writes the scorecard from the command line."""
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "dir", type=pathlib.Path, help="a directory of result directories"
    )
    parser.add_argument(
        "--compare",
        type=pathlib.Path,
        metavar="DIR",
        help="an earlier directory of results to compare with",
    )
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        metavar="DIR",
        help="where to write scorecard.md and scorecard.json (default: dir)",
    )
    args = parser.parse_args(argv)

    try:
        rows, sources = read_results(args.dir)
        before = read_results(args.compare)[0] if args.compare else None
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    if not rows:
        parser.error(f"{args.dir} has no results: */result.json")
    if before is not None and not before:
        parser.error(f"{args.compare} has no results: */result.json")
    meta = {
        "name": args.dir.resolve().name,
        # The newest result's, so the scorecard depends on the results alone
        "date": max(s.provenance["date"] for s in sources),
        "compared_with": args.compare.resolve().name if args.compare else None,
    }
    changes = compare(before, rows) if before is not None else None
    out = args.out or args.dir
    out.mkdir(parents=True, exist_ok=True)
    (out / "scorecard.json").write_text(
        result.to_json_text(to_json(meta, sources, rows, changes))
    )
    # By their paths from where scorecard.md goes
    charts = [
        os.path.relpath(path, out)
        for path in sorted((args.dir / "charts").glob("*.png"))
    ]
    table = markdown(meta, sources, rows, changes, charts)
    (out / "scorecard.md").write_text(table)
    print(table, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
