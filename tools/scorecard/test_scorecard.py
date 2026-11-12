import json
import pathlib
import sys
from typing import Any

import hostcheck
import pytest
import scorecard
import sweep


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
def test_s6_is_worse_only_beyond_the_guardrail(
    before: float, after: float, verdict: str
) -> None:
    change = scorecard.compare_metric(
        "per", metric(before, unit="dB"), metric(after, unit="dB")
    )
    assert change.verdict == verdict
    assert change.difference == pytest.approx(after - before)


def test_s1_is_worse_if_the_tests_or_coverage_drop() -> None:
    before = scorecard.Metric("%", False, [96.8])
    after = scorecard.Metric("%", False, [96.5])
    assert scorecard.compare_metric("tests", before, after).verdict == "worse"
    assert scorecard.compare_metric("tests", after, before).verdict == "better"


def test_compare_skips_metrics_the_earlier_run_didnt_measure() -> None:
    before = {"throughput": {"W-6": metric(1, 2)}}
    after = {"throughput": {"W-6": metric(1, 2), "W-54": metric(3, 4)}}
    assert list(scorecard.compare(before, after)["throughput"]) == ["W-6"]


LCOV = """\
SF:src/a.cpp
LF:10
LH:8
end_of_record
SF:src/phy/test_util.h
LF:5
LH:0
end_of_record
SF:external/x.cpp
LF:100
LH:0
end_of_record
SF:src/b.h
LF:6
LH:6
end_of_record
"""


def test_line_coverage_counts_src_without_the_test_helpers() -> None:
    assert scorecard.line_coverage(LCOV) == (14, 16)


def test_count_tests_leaves_out_disabled_tests() -> None:
    reports = [
        '<testsuites tests="18" failures="0" disabled="1"></testsuites>',
        '<testsuites tests="5" failures="0" disabled="0"></testsuites>',
    ]
    assert scorecard.count_tests(reports) == 22


def bench_result(name: str, rep: int, rate: float) -> dict[str, Any]:
    return {
        "name": f"{name}/real_time",
        "run_name": f"{name}/real_time",
        "run_type": "iteration",
        "repetition_index": rep,
        "samples_per_second": rate,
        "time_per_sample": 1 / rate,
    }


def test_bench_rows_has_s2_and_s3_in_order_from_each_repetition() -> None:
    results = {
        "benchmarks": [
            bench_result("timing_sync/W-54", 0, 50e6),
            bench_result("receiver_chain/W-54", 0, 20e6),
            bench_result("receiver_chain/W-idle", 0, 32e6),
            bench_result("frame_detector/W-54", 0, 62.5e6),
            bench_result("receiver_chain/W-54", 1, 21e6),
            {
                "name": "receiver_chain/W-54/real_time_median",
                "run_name": "receiver_chain/W-54/real_time",
                "run_type": "aggregate",
                "samples_per_second": 20.5e6,
            },
        ]
    }
    rows = scorecard.bench_rows(results)
    assert list(rows["throughput"]) == ["W-idle", "W-54"]
    assert rows["throughput"]["W-54"].samples == pytest.approx([20, 21])
    assert list(rows["block-cost"]) == [
        "frame_detector W-54",
        "timing_sync W-54",
    ]
    assert rows["block-cost"]["timing_sync W-54"].samples == pytest.approx([20])
    assert rows["block-cost"]["timing_sync W-54"].lower_is_better


@pytest.mark.parametrize(
    ("rows", "pattern"),
    [
        (["throughput", "block-cost"], "."),
        (["throughput"], "^receiver_chain/"),
        (["block-cost"], "-^receiver_chain/"),
    ],
)
def test_bench_filter_picks_the_rows_benchmarks(
    rows: list[str], pattern: str
) -> None:
    assert scorecard.bench_filter(rows) == pattern


def histogram(p50: float, p99_9: float, top: float) -> dict[str, float]:
    return {"p50": p50, "p99": p99_9, "p99_9": p99_9, "max": top}


def sim_summary(scale: float) -> dict[str, Any]:
    block = {
        "processing": histogram(1000 * scale, 3000 * scale, 4000),
        "start_interval": histogram(200_000, 250_000, 300_000),
        "wait": histogram(500, 1500, 2000),
        "lag": histogram(10_000, 20_000, 30_000),
    }
    return {
        "metrics": {
            "latency": {
                "p50": 2_000_000,
                "p99": 2_500_000,
                "p99_9": 3_000_000,
                "max": 3_100_000 * scale,
            },
            "blocks": {name: block for name in scorecard.BLOCKS},
        }
    }


def test_sim_rows_has_latency_and_each_blocks_jitter_in_us() -> None:
    rows = scorecard.sim_rows({"W-54": [sim_summary(1), sim_summary(2)]})
    assert rows["latency"]["W-54 p50"].samples == [2000, 2000]
    assert rows["latency"]["W-54 max"].samples == [3100, 6200]
    s5 = rows["jitter"]
    assert s5["W-54 timing_sync processing p50"].samples == [1, 2]
    assert s5["W-54 timing_sync processing jitter"].samples == [2, 4]
    assert s5["W-54 timing_sync start_interval jitter"].samples == [50, 50]
    assert s5["W-54 timing_sync wait jitter"].samples == [1, 1]
    assert s5["W-54 timing_sync backlog"].samples == [30, 30]
    assert len(s5) == 5 * len(scorecard.BLOCKS)


def point(snr: float, lost: int) -> sweep.Point:
    return sweep.Point(snr, 100, lost, 100, lost, 0, 0)


def test_per_rows_has_each_rates_snr_and_none_if_never_reached() -> None:
    rows = scorecard.per_rows(
        {
            "bpsk-1/2": [point(10, 100), point(12, 10)],
            "qam64-3/4": [point(10, 100), point(12, 100)],
        }
    )
    assert rows["bpsk-1/2"].samples == [12]
    assert rows["qam64-3/4"].samples == []


@pytest.mark.parametrize(
    ("cpu", "board", "name"),
    [
        (
            "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz",
            "",
            "intel-core-i7-10700k",
        ),
        (
            "ARM Cortex-A76",
            "Raspberry Pi 5 Model B Rev 1.0",
            "raspberry-pi-5-model-b",
        ),
        (
            "AMD Ryzen 9 7950X 16-Core Processor",
            "",
            "amd-ryzen-9-7950x-16-core-processor",
        ),
    ],
)
def test_host_name_is_a_directory_name_for_the_machine(
    cpu: str, board: str, name: str
) -> None:
    host = make_host(cpu_model=cpu, board=board)
    assert scorecard.host_name(host) == name


def make_host(**changes: Any) -> hostcheck.Host:
    fields: dict[str, Any] = {
        "cpu_model": "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz",
        "board": "",
        "arch": "x86_64",
        "kernel": "6.8.0",
        "kernel_build": "#1 SMP",
        "preempt_rt": False,
        "virtualization": "none",
        "container": "none",
        "cpus_online": 16,
        "cpus_usable": list(range(16)),
        "cpu_quota": None,
        "governors": {"performance": 16},
        "turbo": False,
        "isolated": "",
        "nohz_full": "",
        "load_average": 0.1,
        "throttled": None,
        "on_battery": False,
        "compiler": "clang version 23.1.4",
        "fun_ofdm_version": "2.0.0",
        "commit": "b6254847da79",
        "dirty": False,
    }
    fields.update(changes)
    return hostcheck.Host(**fields)


def test_suggested_cpus_are_the_isolated_ones_or_all_but_cpu_0() -> None:
    assert scorecard.suggested_cpus(make_host()) == "1-15"
    assert scorecard.suggested_cpus(make_host(isolated="4-7")) == "4-7"


def test_grid_puts_s3s_blocks_on_lines_and_workloads_in_columns() -> None:
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


META = {
    "milestone": "milestone2",
    "host": "intel-core-i7-10700k",
    "configuration": "Pinned",
    "date": "2026-11-11T00:00:00+00:00",
    "repetitions": 3,
    "workload_seconds": None,
    "cpus": "1-15",
    "compared_with": "old.json",
}


def test_markdown_has_the_machine_and_a_table_per_row() -> None:
    rows = {
        "throughput": {
            "W-54": scorecard.Metric("MS/s", False, [19.9, 20.0, 20.1])
        },
        "per": {"qam64-3/4": scorecard.Metric("dB", True, [])},
    }
    text = scorecard.markdown(META, make_host(), rows, None)
    assert "# Scorecard: intel-core-i7-10700k, Stock" in text
    assert "the median of 3 runs" in text
    assert "## Receiver throughput (MS/s)" in text
    assert "| W-54 | 20.0 (19.9 to 20.1) |" in text
    assert "| qam64-3/4 | not reached |" in text


def test_markdown_compared_has_the_change_and_its_verdict() -> None:
    before = {"latency": {"W-54 p50": metric(*FAST)}}
    after = {"latency": {"W-54 p50": metric(*SLOW)}}
    changes = scorecard.compare(before, after)
    text = scorecard.markdown(META, make_host(), after, changes)
    assert "Compared with old.json" in text
    line = next(line for line in text.splitlines() if "W-54 p50" in line)
    assert line.startswith("| W-54 p50 | 9.25 | 10.2 (10.1 to 10.4) | +10.8% |")
    assert line.endswith("| worse |")


def test_scorecard_json_round_trips_its_rows() -> None:
    rows = {
        "throughput": {"W-6": scorecard.Metric("MS/s", False, [31.0, 31.2])}
    }
    data = json.loads(
        json.dumps(scorecard.to_json(META, make_host(), rows, None))
    )
    assert data["hostcheck"]["cpu_model"].startswith("Intel")
    assert scorecard.rows_from_json(data) == rows


# Stand-ins for the binaries. fun-ofdm answers --version and writes sim's
# JSON, from fun-ofdm.json next to it, exiting 1 as when frames are lost;
# fun-ofdm-bench writes a result for each benchmark and repetition to
# --benchmark_out, and its arguments to FAKE_ARGS.
FAKE_FUN_OFDM = """\
import sys
if sys.argv[1] == "--version":
    print("2.0.0")
    sys.exit(0)
print(open(sys.argv[0] + ".json").read())
sys.exit(1)
"""

FAKE_BENCH = """\
import json, os, sys
args = dict(a.lstrip("-").split("=", 1) for a in sys.argv[1:])
with open(os.environ["FAKE_ARGS"], "w") as f:
    json.dump(args, f)
reps = int(args["benchmark_repetitions"])
names = ["receiver_chain/W-54"] + [
    f"{b}/{w}" for b in ["frame_detector", "timing_sync"] for w in ["W-6", "W-54"]
]
results = [
    {"run_name": n + "/real_time", "run_type": "iteration",
     "samples_per_second": 20e6 + i * 1e6, "time_per_sample": 1 / (20e6 + i * 1e6)}
    for i in range(reps) for n in names
]
with open(args["benchmark_out"], "w") as f:
    json.dump({"benchmarks": results}, f)
"""


@pytest.fixture
def bin_dir(tmp_path: pathlib.Path) -> pathlib.Path:
    directory = tmp_path / "bin"
    directory.mkdir()
    for name, script in (
        ("fun-ofdm", FAKE_FUN_OFDM),
        ("fun-ofdm-bench", FAKE_BENCH),
    ):
        path = directory / name
        path.write_text(f"#!{sys.executable}\n{script}")
        path.chmod(0o755)
    (directory / "fun-ofdm.json").write_text(json.dumps(sim_summary(1)))
    return directory


def test_main_writes_the_scorecard(
    bin_dir: pathlib.Path,
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    args = tmp_path / "args.json"
    monkeypatch.setenv("FAKE_ARGS", str(args))
    out = tmp_path / "perf"
    status = scorecard.main(
        [
            "--milestone",
            "milestone2",
            "--host",
            "test-host",
            "--rows",
            "throughput",
            "block-cost",
            "latency",
            "jitter",
            "--repetitions",
            "2",
            "--workload-seconds",
            "0.1",
            "--bin-dir",
            str(bin_dir),
            "--out-dir",
            str(out),
        ]
    )
    assert status == 0
    bench_args = json.loads(args.read_text())
    assert bench_args["benchmark_filter"] == "."
    assert bench_args["benchmark_repetitions"] == "2"
    assert bench_args["workload_seconds"] == "0.1"

    [result] = (out / "milestone2" / "test-host").glob("*.json")
    data = json.loads(result.read_text())
    assert list(data["rows"]) == [
        "throughput",
        "block-cost",
        "latency",
        "jitter",
    ]
    assert data["rows"]["throughput"]["W-54"]["samples"] == pytest.approx(
        [20, 21]
    )
    assert data["rows"]["latency"]["W-54 p50"]["samples"] == [2000, 2000]
    assert data["repetitions"] == 2
    assert result.with_suffix(".md").read_text() in capsys.readouterr().out


def test_main_compares_with_an_earlier_run(
    bin_dir: pathlib.Path,
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("FAKE_ARGS", str(tmp_path / "args.json"))
    out = tmp_path / "perf"
    common = ["--milestone", "m", "--host", "h", "--rows", "throughput"]
    common += ["--repetitions", "2", "--bin-dir", str(bin_dir)]
    common += ["--out-dir", str(out)]
    assert scorecard.main(common) == 0
    [first] = (out / "m" / "h").glob("*.json")
    old = tmp_path / "old.json"
    first.rename(old)
    assert scorecard.main([*common, "--compare", str(old)]) == 0
    data = json.loads(first.read_text())
    assert data["changes"]["throughput"]["W-54"]["verdict"] == "same"
    assert "| W-54 | 20.5 |" in first.with_suffix(".md").read_text()


def test_main_needs_the_binaries(tmp_path: pathlib.Path) -> None:
    with pytest.raises(SystemExit):
        scorecard.main(
            ["--milestone", "m", "--bin-dir", str(tmp_path / "missing")]
        )
