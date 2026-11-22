import json
import pathlib
import sys
from typing import Any

import hostcheck
import measure
import pytest
import scorecard

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
    assert measure.line_coverage(LCOV) == (14, 16)


def test_count_tests_leaves_out_disabled_tests() -> None:
    reports = [
        '<testsuites tests="18" failures="0" disabled="1"></testsuites>',
        '<testsuites tests="5" failures="0" disabled="0"></testsuites>',
    ]
    assert measure.count_tests(reports) == 22


def bench_result(name: str, rep: int, rate: float) -> dict[str, Any]:
    return {
        "name": f"{name}/real_time",
        "run_name": f"{name}/real_time",
        "run_type": "iteration",
        "repetition_index": rep,
        "samples_per_second": rate,
        "time_per_sample": 1 / rate,
    }


def test_bench_rows_has_throughput_and_block_cost_from_each_repetition() -> (
    None
):
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
    rows = measure.bench_rows(results)
    assert list(rows["throughput"]) == ["W-idle", "W-54"]
    assert rows["throughput"]["W-54"].samples == pytest.approx([20, 21])
    assert list(rows["block-cost"]) == [
        "frame_detector W-54",
        "timing_sync W-54",
    ]
    assert rows["block-cost"]["timing_sync W-54"].samples == pytest.approx([20])
    assert rows["block-cost"]["timing_sync W-54"].lower_is_better


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
    rows = measure.sim_rows({"W-54": [sim_summary(1), sim_summary(2)]})
    assert rows["latency"]["W-54 p50"].samples == [2000, 2000]
    assert rows["latency"]["W-54 max"].samples == [3100, 6200]
    s5 = rows["jitter"]
    assert s5["W-54 timing_sync processing p50"].samples == [1, 2]
    assert s5["W-54 timing_sync processing jitter"].samples == [2, 4]
    assert s5["W-54 timing_sync start_interval jitter"].samples == [50, 50]
    assert s5["W-54 timing_sync wait jitter"].samples == [1, 1]
    assert s5["W-54 timing_sync backlog"].samples == [30, 30]
    assert len(s5) == 5 * len(scorecard.BLOCKS)


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
    assert measure.suggested_cpus(make_host()) == "1-15"
    assert measure.suggested_cpus(make_host(isolated="4-7")) == "4-7"


def test_adds_histograms_buckets_together() -> None:
    assert measure.add_buckets([[1, 2], [5, 1]], [[1, 1], [3, 4]]) == [
        [1, 3],
        [3, 4],
        [5, 1],
    ]


@pytest.mark.parametrize(
    ("value", "rounded"), [(7, 7), (99, 99), (101, 110), (1234, 1300)]
)
def test_rounds_buckets_up_to_2_significant_digits(
    value: int, rounded: int
) -> None:
    assert measure.coarse(value) == rounded


def test_merges_buckets_that_round_to_the_same_value() -> None:
    assert measure.add_buckets([], [[1201, 1], [1255, 2], [1301, 4]]) == [
        [1300, 3],
        [1400, 4],
    ]


def test_merges_each_runs_histograms_across_repetitions() -> None:
    def summary(count: int) -> dict[str, Any]:
        histogram = {"buckets": [[100, count]]}
        return {
            "metrics": {
                "latency": histogram,
                "blocks": {"fft_symbols": {"processing": histogram}},
            }
        }

    merged = measure.merged_histograms({"W-6": [summary(1), summary(2)]})
    assert merged == {
        "W-6": {
            "latency": [[100, 3]],
            "blocks": {"fft_symbols": {"processing": [[100, 3]]}},
        }
    }


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


def buckets_summary(scale: float) -> dict[str, Any]:
    # sim_summary, with the buckets that merged_histograms() reads.
    summary = sim_summary(scale)
    metrics = summary["metrics"]
    metrics["latency"]["buckets"] = [[2_000_000, 1]]
    metrics["blocks"] = {
        name: {k: {**v, "buckets": [[1000, 1]]} for k, v in block.items()}
        for name, block in metrics["blocks"].items()
    }
    return summary


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
    (directory / "fun-ofdm.json").write_text(json.dumps(buckets_summary(1)))
    return directory


def test_main_measures_the_benchmarks(
    bin_dir: pathlib.Path,
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    args = tmp_path / "args.json"
    monkeypatch.setenv("FAKE_ARGS", str(args))
    out = tmp_path / "bench"
    status = measure.main(
        [
            "bench",
            "--repetitions",
            "2",
            "--workload-seconds",
            "0.1",
            "--bin-dir",
            str(bin_dir),
            "--out",
            str(out),
        ]
    )
    assert status == 0
    bench_args = json.loads(args.read_text())
    assert bench_args["benchmark_repetitions"] == "2"
    assert bench_args["workload_seconds"] == "0.1"
    data = json.loads((out / "result.json").read_text())
    assert data["provenance"]["tool"] == "tools/scorecard/measure.py bench"
    assert data["provenance"]["options"] == {
        "repetitions": 2,
        "workload_seconds": 0.1,
        "filter": None,
        "trace": False,
    }
    assert list(data["provenance"]["binaries"]) == ["fun-ofdm-bench"]
    assert list(data["rows"]) == ["throughput", "block-cost"]
    assert data["rows"]["throughput"]["W-54"]["samples"] == pytest.approx(
        [20, 21]
    )
    assert "## Receiver throughput" in (out / "result.md").read_text()


def test_main_measures_the_latency(
    bin_dir: pathlib.Path, tmp_path: pathlib.Path
) -> None:
    out = tmp_path / "latency"
    status = measure.main(
        [
            "latency",
            "--repetitions",
            "2",
            "--bin-dir",
            str(bin_dir),
            "--out",
            str(out),
        ]
    )
    assert status == 0
    data = json.loads((out / "result.json").read_text())
    assert list(data["rows"]) == ["latency", "jitter"]
    assert data["rows"]["latency"]["W-54 p50"]["samples"] == [2000, 2000]
    assert data["histograms"]["W-6"]["latency"] == [[2_000_000, 2]]
    # The scorecard reads it
    rows, _ = scorecard.read_results(tmp_path)
    assert list(rows) == ["latency", "jitter"]


def test_main_needs_the_binaries(tmp_path: pathlib.Path) -> None:
    with pytest.raises(SystemExit):
        measure.main(
            [
                "bench",
                "--out",
                str(tmp_path / "bench"),
                "--bin-dir",
                str(tmp_path / "missing"),
            ]
        )


def test_main_passes_on_a_filter_and_a_trace(
    bin_dir: pathlib.Path,
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    args = tmp_path / "args.json"
    monkeypatch.setenv("FAKE_ARGS", str(args))
    out = tmp_path / "bench"
    trace = tmp_path / "bench.pftrace"
    measure.main(
        [
            "bench",
            "--repetitions",
            "1",
            "--filter",
            "^receiver_chain/",
            "--trace",
            str(trace),
            "--bin-dir",
            str(bin_dir),
            "--out",
            str(out),
        ]
    )
    bench_args = json.loads(args.read_text())
    assert bench_args["benchmark_filter"] == "^receiver_chain/"
    assert bench_args["trace"] == str(trace)
    options = json.loads((out / "result.json").read_text())["provenance"][
        "options"
    ]
    assert options["filter"] == "^receiver_chain/"
    assert options["trace"] is True
