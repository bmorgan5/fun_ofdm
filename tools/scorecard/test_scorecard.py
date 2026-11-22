import json
import pathlib
from typing import Any

import pytest
import result
import scorecard


def metric(*samples: float, unit: str = "µs") -> scorecard.Metric:
    return scorecard.Metric(unit, True, list(samples))


def test_metric_has_the_median_and_quartiles() -> None:
    m = metric(1, 2, 3, 4, 5)
    assert m.median == 3
    assert m.quartiles == (2, 4)


def test_a_metric_measured_once_is_its_own_quartiles() -> None:
    assert metric(7).quartiles == (7, 7)


def test_a_metric_without_samples_has_no_median() -> None:
    assert metric().median is None
    assert metric().quartiles is None


def test_metric_round_trips_through_json() -> None:
    m = metric(1, 2, 3)
    assert scorecard.Metric.from_json(json.loads(json.dumps(m.to_json()))) == m


# Known values: SciPy's mannwhitneyu(method="asymptotic") for each pair, to
# 10 decimal places.
@pytest.mark.parametrize(
    ("a", "b", "p"),
    [
        (
            [19.8, 20.1, 20.0, 19.9, 20.2],
            [20.5, 20.6, 20.4, 20.7, 20.3],
            0.0121857804,
        ),
        (
            [1, 2, 3, 4, 5, 6, 7, 8, 9, 10] * 2,
            [5, 6, 7, 8, 9, 10, 11, 12, 13, 14] * 2,
            0.0005402968,
        ),
        ([1, 1, 2, 2, 3], [1, 2, 2, 3, 3], 0.5067287123),
        ([1.0, 2.0], [3.0, 4.0, 5.0], 0.1489146732),
    ],
)
def test_mann_whitney_p_matches_scipy(
    a: list[float], b: list[float], p: float
) -> None:
    assert scorecard.mann_whitney_p(a, b) == pytest.approx(p, abs=1e-10)


def test_mann_whitney_p_is_1_when_every_value_is_the_same() -> None:
    assert scorecard.mann_whitney_p([3, 3, 3], [3, 3, 3]) == 1.0


SLOW = [10.0, 10.1, 10.2, 10.3, 10.4, 10.5]
FAST = [9.0, 9.1, 9.2, 9.3, 9.4, 9.5]


def test_a_significant_rise_in_a_time_is_worse() -> None:
    change = scorecard.compare_metric("latency", metric(*FAST), metric(*SLOW))
    assert change.verdict == "worse"
    assert change.p is not None
    assert change.p < 0.05
    assert change.difference == pytest.approx((10.25 - 9.25) / 9.25 * 100)


def test_a_significant_rise_in_throughput_is_better() -> None:
    before = scorecard.Metric("MS/s", False, FAST)
    after = scorecard.Metric("MS/s", False, SLOW)
    assert (
        scorecard.compare_metric("throughput", before, after).verdict
        == "better"
    )


def test_a_change_within_chance_is_the_same() -> None:
    change = scorecard.compare_metric(
        "latency", metric(10, 12, 11, 13), metric(11, 13, 12, 10)
    )
    assert change.verdict == "same"


@pytest.mark.parametrize(
    ("before", "after", "verdict"),
    [(18.0, 18.3, "worse"), (18.0, 18.2, "same"), (18.0, 17.5, "better")],
)
def test_an_snr_in_simulation_changes_only_beyond_the_guardrail(
    before: float, after: float, verdict: str
) -> None:
    change = scorecard.compare_metric(
        "per", metric(before, unit="dB"), metric(after, unit="dB")
    )
    assert change.verdict == verdict
    assert change.difference == pytest.approx(after - before)


def test_the_tests_or_coverage_are_worse_if_they_drop() -> None:
    before = scorecard.Metric("%", False, [96.8])
    after = scorecard.Metric("%", False, [96.5])
    assert scorecard.compare_metric("tests", before, after).verdict == "worse"
    assert scorecard.compare_metric("tests", after, before).verdict == "better"


def test_compare_skips_metrics_the_earlier_run_didnt_measure() -> None:
    before = {"throughput": {"W-6": metric(1, 2)}}
    after = {"throughput": {"W-6": metric(1, 2), "W-54": metric(3, 4)}}
    assert list(scorecard.compare(before, after)["throughput"]) == ["W-6"]


def test_grid_puts_blocks_on_lines_and_workloads_in_columns() -> None:
    rows = {
        "frame_detector W-6": metric(16.0),
        "frame_detector W-54": metric(16.1),
        "timing_sync W-6": metric(13.0),
        "timing_sync W-54": metric(18.3),
    }
    assert scorecard.grid("block-cost", rows) == [
        "| | W-6 | W-54 |",
        "|---|---|---|",
        "| frame_detector | 16.0 | 16.1 |",
        "| timing_sync | 13.0 | 18.3 |",
    ]


def prov(tool: str) -> dict[str, Any]:
    # Enough provenance for the scorecard: the tool, and what describe()
    # reads.
    return {
        "tool": tool,
        "options": {},
        "date": "2026-11-22T00:00:00+00:00",
        "binaries": {},
        "sources": {},
        "host": {"cpu_model": "ARM Cortex-A76", "configuration": "Pinned"},
        "hardware": None,
    }


def write(directory: pathlib.Path, tool: str, data: dict[str, Any]) -> None:
    directory.mkdir(parents=True)
    (directory / "result.json").write_text(
        json.dumps({"provenance": prov(tool), **data})
    )


THROUGHPUT = {
    "rows": {
        "throughput": {
            "W-54": scorecard.Metric(
                "MS/s", False, [19.9, 20.0, 20.1]
            ).to_json()
        }
    }
}

PER = {
    "rates": {
        "bpsk-1/2": {"snr_at_target_per": 18.1},
        "qam64-3/4": {"snr_at_target_per": None},
    }
}

REALTIME = {"highest_kept_up_mhz": {"bpsk-1/2": 7.0, "idle": None}}

PER_HARDWARE = {
    "rates": {
        "bpsk-1/2": {
            "snr_at_target_per": 17.5,
            "implementation_loss_db": -0.6,
        }
    }
}

LINK = {
    "runs": {
        "bpsk-1/2": {
            "summary": {"sent": {"packets": 1000}, "received": {"packets": 999}}
        },
        "qam64-3/4": {"summary": None},
    }
}


@pytest.fixture
def results(tmp_path: pathlib.Path) -> pathlib.Path:
    directory = tmp_path / "milestone2"
    write(directory / "bench", "tools/scorecard/measure.py bench", THROUGHPUT)
    write(directory / "per", "tools/per/sweep.py", PER)
    write(
        directory / "realtime",
        "tools/hardware/hardware_sweep.py realtime",
        REALTIME,
    )
    write(
        directory / "per-hardware",
        "tools/hardware/hardware_sweep.py per",
        PER_HARDWARE,
    )
    write(directory / "link_smoke", "hil/test_link_smoke.py", LINK)
    # Another set of results inside it, which isn't read
    write(directory / "repeat" / "bench", "tools/scorecard/measure.py x", {})
    return directory


def test_reads_every_tools_results_into_rows(results: pathlib.Path) -> None:
    rows, sources = scorecard.read_results(results)
    assert list(rows) == [
        "throughput",
        "per",
        "realtime",
        "per-hardware",
        "link",
    ]
    assert rows["throughput"]["W-54"].samples == [19.9, 20.0, 20.1]
    assert rows["per"]["bpsk-1/2"].samples == [18.1]
    assert rows["per"]["qam64-3/4"].samples == []
    assert rows["realtime"]["bpsk-1/2"].samples == [7.0]
    assert rows["realtime"]["idle"].samples == []
    assert rows["per-hardware"]["bpsk-1/2 SNR"].samples == [17.5]
    assert rows["per-hardware"]["bpsk-1/2 loss"].samples == [-0.6]
    assert rows["link"]["bpsk-1/2"].samples == [99.9]
    assert rows["link"]["qam64-3/4"].samples == []
    assert [s.path for s in sources] == [
        "bench",
        "link_smoke",
        "per",
        "per-hardware",
        "realtime",
    ]


def test_refuses_two_results_with_the_same_row(results: pathlib.Path) -> None:
    write(results / "per-again", "tools/per/sweep.py", PER)
    with pytest.raises(ValueError, match="another result has per"):
        scorecard.read_results(results)


def test_refuses_a_tool_it_doesnt_read(results: pathlib.Path) -> None:
    write(results / "other", "tools/other.py", {})
    with pytest.raises(ValueError, match=r"doesn't read tools/other\.py"):
        scorecard.read_results(results)


@pytest.mark.parametrize(
    ("row", "before", "after", "verdict"),
    [
        ("per-hardware", 18.0, 18.9, "same"),
        ("per-hardware", 18.0, 19.1, "worse"),
        ("link", 99.9, 99.0, "same"),
        ("link", 99.9, 98.0, "worse"),
        ("realtime", 7.0, 8.0, "better"),
    ],
)
def test_a_metric_measured_once_changes_beyond_its_rows_tolerance(
    row: str, before: float, after: float, verdict: str
) -> None:
    lower = row == "per-hardware"
    change = scorecard.compare_metric(
        row,
        scorecard.Metric("dB", lower, [before]),
        scorecard.Metric("dB", lower, [after]),
    )
    assert change.verdict == verdict


META = {
    "name": "milestone2",
    "date": "2026-11-22T00:00:00+00:00",
    "compared_with": "milestone1",
}


def test_markdown_has_its_sources_and_a_table_per_row(
    results: pathlib.Path,
) -> None:
    rows, sources = scorecard.read_results(results)
    text = scorecard.markdown(META, sources, rows, None)
    assert "# Scorecard: milestone2" in text
    assert f"- `per/`: {result.describe(prov('tools/per/sweep.py'))}" in text
    assert "## Receiver throughput (MS/s)" in text
    assert "| W-54 | 20.0 (19.9 to 20.1) |" in text
    assert "| qam64-3/4 | not reached |" in text
    assert "| | SNR | loss |" in text
    assert "| bpsk-1/2 | 17.5 | -0.60 |" in text


def test_markdown_compared_has_the_change_and_its_verdict() -> None:
    before = {"latency": {"W-54 p50": metric(*FAST)}}
    after = {"latency": {"W-54 p50": metric(*SLOW)}}
    changes = scorecard.compare(before, after)
    text = scorecard.markdown(META, [], after, changes)
    assert "Compared with milestone1" in text
    line = next(line for line in text.splitlines() if "W-54 p50" in line)
    assert line.startswith("| W-54 p50 | 9.25 | 10.2 (10.1 to 10.4) | +10.8% |")
    assert line.endswith("| worse |")


def test_main_writes_the_scorecard(
    results: pathlib.Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert scorecard.main([str(results)]) == 0
    data = json.loads((results / "scorecard.json").read_text())
    assert data["name"] == "milestone2"
    assert data["rows"]["per"]["bpsk-1/2"]["median"] == 18.1
    assert data["sources"][0]["path"] == "bench"
    text = (results / "scorecard.md").read_text()
    assert text in capsys.readouterr().out


def test_main_compares_with_an_earlier_directory(
    results: pathlib.Path, tmp_path: pathlib.Path
) -> None:
    old = tmp_path / "milestone1"
    write(old / "per", "tools/per/sweep.py", PER)
    out = tmp_path / "out"
    assert (
        scorecard.main([str(results), "--compare", str(old), "--out", str(out)])
        == 0
    )
    data = json.loads((out / "scorecard.json").read_text())
    assert data["changes"]["per"]["bpsk-1/2"]["verdict"] == "same"
    assert (
        "throughput" not in data["changes"] or not data["changes"]["throughput"]
    )


def test_main_needs_results(tmp_path: pathlib.Path) -> None:
    with pytest.raises(SystemExit):
        scorecard.main([str(tmp_path)])


def test_a_change_in_mhz_is_in_mhz() -> None:
    change = scorecard.compare_metric(
        "realtime",
        scorecard.Metric("MHz", False, [7.0]),
        scorecard.Metric("MHz", False, [8.0]),
    )
    assert scorecard.change_text(change, "MHz") == "+1.00 MHz"


@pytest.mark.parametrize(
    ("before", "after", "verdict"),
    [([18.0], [], "worse"), ([], [18.0], "better"), ([], [], "same")],
)
def test_a_metric_no_longer_measured_is_worse(
    before: list[float], after: list[float], verdict: str
) -> None:
    change = scorecard.compare_metric(
        "per",
        scorecard.Metric("dB", True, before),
        scorecard.Metric("dB", True, after),
    )
    assert change.verdict == verdict


def test_refuses_an_interrupted_sweep(results: pathlib.Path) -> None:
    write(
        results / "realtime-again",
        "tools/hardware/hardware_sweep.py realtime",
        {**REALTIME, "complete": False},
    )
    with pytest.raises(ValueError, match="interrupted"):
        scorecard.read_results(results)


def test_says_which_result_it_cant_read(tmp_path: pathlib.Path) -> None:
    write(tmp_path / "per", "tools/per/sweep.py", {"not": "rates"})
    with pytest.raises(ValueError, match=r"per/result\.json: can't read"):
        scorecard.read_results(tmp_path)


def test_notes_results_from_different_builds(results: pathlib.Path) -> None:
    _, sources = scorecard.read_results(results)
    assert scorecard.consistency_notes(sources) == []
    sources[0].provenance["binaries"] = {"fun-ofdm": {"sha256": "a" * 64}}
    sources[1].provenance["binaries"] = {"fun-ofdm": {"sha256": "b" * 64}}
    sources[1].provenance["host"] = {
        "commit": "0123456789abcdef",
        "dirty": True,
    }
    notes = scorecard.consistency_notes(sources)
    assert notes[0].startswith("The results ran 2 different fun-ofdm binaries")
    assert notes[1] == "The results are from commits 0123456789ab, unknown"
    assert notes[2] == "These had uncommitted changes: link_smoke/"


def test_main_needs_results_to_compare_with(
    results: pathlib.Path, tmp_path: pathlib.Path
) -> None:
    with pytest.raises(SystemExit):
        scorecard.main([str(results), "--compare", str(tmp_path / "missing")])


def test_main_dates_the_scorecard_by_its_newest_result(
    results: pathlib.Path,
) -> None:
    scorecard.main([str(results)])
    data = json.loads((results / "scorecard.json").read_text())
    assert data["date"] == "2026-11-22T00:00:00+00:00"
