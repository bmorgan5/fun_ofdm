import json
import pathlib
from typing import Any

import charts
import pytest
import scorecard

BUCKETS = [[100, 50], [200, 40], [300, 9], [1000, 1]]


def test_finds_a_percentile_in_the_buckets() -> None:
    assert charts.percentile(BUCKETS, 0.5) == 100
    assert charts.percentile(BUCKETS, 0.51) == 200
    assert charts.percentile(BUCKETS, 0.99) == 300
    assert charts.percentile(BUCKETS, 1.0) == 1000
    assert charts.percentile([], 0.5) is None


def test_puts_each_nine_a_step_apart() -> None:
    assert charts.nines(0.9) == pytest.approx(1)
    assert charts.nines(0.999) == pytest.approx(3)


@pytest.mark.parametrize(
    ("fraction", "count", "rank"),
    [(0.99, 10, 10), (0.5, 3, 2), (0.5, 4, 2), (0.99, 100, 99), (0.0, 5, 1)],
)
def test_rounds_to_the_nearest_rank_as_hdrhistogram_does(
    fraction: float, count: int, rank: int
) -> None:
    # hdr_value_at_percentile(): (int64_t)(p / 100 * total + 0.5), at least 1
    assert charts.hdr_rank(fraction, count) == rank


def test_takes_a_percentile_by_hdrhistograms_rank() -> None:
    values = list(range(1, 11))
    assert charts.nearest_rank(values, 50) == 5
    assert charts.nearest_rank(values, 99) == 10
    assert charts.nearest_rank(values, 94) == 9


SLICES = [
    ("timing_sync", "timing_sync", 1000 * i, 1000 * (i % 10 + 1))
    for i in range(100)
] + [("fun-ofdm", "arrival", 0, 1)]


def summary(count: int, p50: int, p99: int) -> dict[str, Any]:
    return {
        "metrics": {
            "blocks": {
                "timing_sync": {
                    "processing": {"count": count, "p50": p50, "p99": p99}
                }
            }
        }
    }


def test_compares_the_histograms_with_a_complete_trace() -> None:
    rows = charts.agreement_rows("W-6", summary(100, 5000, 10000), SLICES)
    assert [r["Percentile"] for r in rows] == ["p50", "p99"]
    assert rows[0]["Slices"] == "all"
    assert rows[0]["Difference"] == "+0.00%"
    assert rows[1]["Trace (µs)"] == "10.0"


def test_says_when_the_trace_has_fewer_runs_than_the_histogram() -> None:
    rows = charts.agreement_rows("idle", summary(1000, 5000, 10000), SLICES)
    assert rows[0]["Slices"] == "100 of 1000"


def test_the_worst_difference_is_of_complete_traces_only() -> None:
    complete = charts.agreement_rows("a", summary(100, 5050, 10000), SLICES)
    partial = charts.agreement_rows("b", summary(1000, 9000, 10000), SLICES)
    text = charts.markdown([], complete + partial)
    assert "**Fail:** where the trace has every slice, the largest " in text
    assert "difference is 1.00%" in text


def test_passes_when_the_complete_traces_agree() -> None:
    rows = charts.agreement_rows("a", summary(100, 5000, 10000), SLICES)
    assert "**Pass:**" in charts.markdown([], rows)


def test_says_when_no_trace_can_be_checked() -> None:
    rows = charts.agreement_rows("b", summary(1000, 5000, 10000), SLICES)
    assert "nothing is checked" in charts.markdown([], rows)


def per_results() -> tuple[dict[str, Any], dict[str, Any]]:
    sim = {
        "rates": {
            "bpsk-1/2": {
                "points": [
                    {"snr_db": 17.0, "per": 0.3, "sent": 1000},
                    {"snr_db": 19.0, "per": 0.0, "sent": 1000},
                ]
            }
        }
    }
    hardware = {
        "rates": {
            "bpsk-1/2": {
                "steps": [
                    {
                        "snr_db": 16.0,
                        "per": 0.5,
                        "sent": 500,
                        "status": "finished",
                    },
                    {
                        "snr_db": 17.0,
                        "per": 1.0,
                        "sent": 500,
                        "status": "crashed",
                    },
                    {
                        "snr_db": 18.0,
                        "per": 0.0,
                        "sent": 500,
                        "status": "finished",
                    },
                ]
            }
        }
    }
    return sim, hardware


def test_draws_per_vs_snr(tmp_path: pathlib.Path) -> None:
    sim, hardware = per_results()
    # A step that sent nothing doesn't divide by 0
    hardware["rates"]["bpsk-1/2"]["steps"].append(
        {"snr_db": 19.0, "per": 1.0, "sent": 0, "status": "finished"}
    )
    assert charts.per_chart(sim, hardware, tmp_path) == tmp_path / "per.png"
    assert (tmp_path / "per.png").stat().st_size > 0
    assert charts.per_chart(None, None, tmp_path / "none") is None


HISTOGRAMS = {
    "latency": BUCKETS,
    "blocks": {
        block: {
            "processing": BUCKETS,
            "start_interval": BUCKETS,
            "wait": BUCKETS,
        }
        for block in scorecard.BLOCKS
    },
}

REALTIME = {
    "highest_kept_up_mhz": {"bpsk-1/2": 5.0, "idle": None},
    "steps": [
        {
            "workload": workload,
            "bandwidth_mhz": bandwidth,
            "overflows": overflows,
            "latency_us": {"p99": 6000.0},
            "cpu_percent": {"cpu0": 50.0, "cpu1": 70.0},
        }
        for workload in ("bpsk-1/2", "idle")
        for bandwidth, overflows in ((5.0, 0), (10.0, 4))
    ],
}


def test_draws_each_blocks_times_and_the_real_time_chart(
    tmp_path: pathlib.Path,
) -> None:
    assert charts.block_times_chart("W-6", HISTOGRAMS, tmp_path).exists()
    assert charts.block_jitter_chart("W-6", HISTOGRAMS, tmp_path).exists()
    assert charts.realtime_chart(REALTIME, tmp_path).exists()


def test_draws_a_timeline(tmp_path: pathlib.Path) -> None:
    chart = charts.timeline_chart("bpsk-1-2-5MHz", SLICES, tmp_path)
    assert chart == tmp_path / "timeline-bpsk-1-2-5MHz.png"
    assert charts.timeline_chart("empty", [], tmp_path) is None


def test_picks_the_traces_either_side_of_the_limit(
    tmp_path: pathlib.Path,
) -> None:
    traces = tmp_path / "traces"
    traces.mkdir()
    for name in ("bpsk-1-2-5MHz", "bpsk-1-2-10MHz", "idle-5MHz", "idle-10MHz"):
        (traces / f"{name}.pftrace.gz").touch()
    assert charts.timeline_traces(tmp_path, REALTIME) == [
        "bpsk-1-2-5MHz",
        "bpsk-1-2-10MHz",
        # Idle never kept up, so only the lowest bandwidth
        "idle-5MHz",
    ]


def write(directory: pathlib.Path, tool: str, data: dict[str, Any]) -> None:
    directory.mkdir(parents=True)
    (directory / "result.json").write_text(
        json.dumps({"provenance": {"tool": tool}, **data})
    )


def test_draws_every_chart_its_results_allow(tmp_path: pathlib.Path) -> None:
    sim, hardware = per_results()
    write(tmp_path / "per", "tools/per/sweep.py", sim)
    write(tmp_path / "hw", "tools/hardware/hardware_sweep.py per", hardware)
    write(
        tmp_path / "latency",
        "tools/scorecard/measure.py latency",
        {"histograms": {"W-6": HISTOGRAMS}},
    )
    (tmp_path / "latency" / "traces").mkdir()
    (tmp_path / "latency" / "traces" / "W-6.pftrace.gz").touch()
    (tmp_path / "latency" / "traces" / "W-6.json").write_text(
        json.dumps(summary(100, 5000, 10000))
    )
    write(
        tmp_path / "realtime",
        "tools/hardware/hardware_sweep.py realtime",
        REALTIME,
    )
    (tmp_path / "realtime" / "traces").mkdir()
    (tmp_path / "realtime" / "traces" / "bpsk-1-2-5MHz.pftrace.gz").touch()

    drawn, agreement = charts.draw(tmp_path, lambda _: SLICES)
    assert [c.name for c in drawn] == [
        "per.png",
        "block-times-W-6.png",
        "block-jitter-W-6.png",
        "realtime.png",
        "timeline-bpsk-1-2-5MHz.png",
    ]
    assert {r["Run"] for r in agreement} == {"sim W-6"}
    text = charts.markdown(drawn, agreement)
    assert "![per](per.png)" in text


def test_refuses_an_interrupted_sweep(tmp_path: pathlib.Path) -> None:
    write(
        tmp_path / "realtime",
        "tools/hardware/hardware_sweep.py realtime",
        {**REALTIME, "complete": False},
    )
    with pytest.raises(ValueError, match="interrupted"):
        charts.draw(tmp_path, lambda _: SLICES)


def test_clears_the_last_runs_charts(tmp_path: pathlib.Path) -> None:
    (tmp_path / "charts").mkdir()
    (tmp_path / "charts" / "timeline-old.png").touch()
    charts.draw(tmp_path, lambda _: SLICES)
    assert not (tmp_path / "charts" / "timeline-old.png").exists()
