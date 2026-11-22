import pathlib
import time
from collections.abc import Sequence
from typing import Any

import hardware
import hardware_sweep as sweep
import pytest

PROC_STAT = """\
cpu  400 0 200 1000 0 0 0 0 0 0
cpu0 100 0 50 250 0 0 0 0 0 0
cpu1 300 0 150 750 50 0 0 0 0 0
intr 12345
"""

LATER = """\
cpu  500 0 300 1100 0 0 0 0 0 0
cpu0 175 0 75 250 0 0 0 0 0 0
cpu1 300 0 150 800 100 0 0 0 0 0
"""


def test_reads_each_cpus_busy_and_total_ticks() -> None:
    assert sweep.parse_proc_stat(PROC_STAT) == {
        "cpu0": (150, 400),
        "cpu1": (450, 1250),
    }


def test_works_out_each_cpus_busy_fraction() -> None:
    fractions = sweep.busy_fractions(
        sweep.parse_proc_stat(PROC_STAT), sweep.parse_proc_stat(LATER)
    )
    assert fractions == {"cpu0": 1.0, "cpu1": 0.0}


def test_a_cpus_use_is_the_90th_percentile_of_its_samples() -> None:
    samples = [{"cpu0": i / 10} for i in range(10)]
    assert sweep.cpu_summary(samples) == {"cpu0": 90.0}
    assert sweep.cpu_summary([]) == {}


def test_samples_the_cpus_until_stopped(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(sweep, "CPU_SAMPLE_S", 0.01)
    readings = iter([PROC_STAT] + [LATER] * 1000)
    deadline = time.monotonic() + 10
    with sweep.CpuSampler(lambda: next(readings)) as sampler:
        while not sampler.samples and time.monotonic() < deadline:
            time.sleep(0.01)
    assert sampler.samples[0] == {"cpu0": 1.0, "cpu1": 0.0}


USRP = hardware.Usrp(serial="F61912", product="B210")
RADIO = {
    "channel": 36,
    "tx_gain": 80,
    "rx_gain": 60,
    "tx_port": "B:TX/RX",
    "rx_port": "B:RX2",
}


def test_an_idle_step_runs_rx_for_its_seconds() -> None:
    args, limit = sweep.step_args(sweep.Step("idle", 20.0, 1), USRP, RADIO, 5)
    assert args == [
        "rx",
        "--device=serial=F61912",
        "--channel=36",
        "--bandwidth=20.0",
        "--gain=60",
    ]
    assert limit == sweep.IDLE_START_S + 5


def test_a_step_with_frames_sends_its_seconds_worth() -> None:
    args, limit = sweep.step_args(
        sweep.Step("bpsk-1/2", 5.0, 1), USRP, RADIO, 5
    )
    frame = hardware.frame_seconds("bpsk-1/2", 5.0)
    packets = round(5 / (frame + sweep.FRAME_GAP_S))
    assert f"--packets={packets}" in args
    assert "--bandwidth=5.0" in args
    assert args[0] == "loopback"
    assert limit == pytest.approx(
        sweep.RUN_START_S + packets * (2 * frame + sweep.FRAME_LIMIT_GAP_S)
    )


def summary(
    overflows: int = 0, sent: int = 100, received: int = 98, lost: Any = 2
) -> dict[str, Any]:
    processing = {"p50": 1000, "p99": 3000}
    data: dict[str, Any] = {
        "sent": {"packets": sent},
        "received": {"packets": received},
        "metrics": {
            "overflows": overflows,
            "arrival_interval": {"count": 100},
            "latency": {"p50": 5000, "p99": 7000},
            "blocks": {
                block: {"processing": processing} for block in sweep.BLOCKS
            },
        },
    }
    if lost is not None:
        data["lost"] = lost
    return data


def run(status: str, data: dict[str, Any] | None) -> hardware.Run:
    return hardware.Run(
        name="x",
        status=status,
        seconds=10.0,
        limit_s=30.0,
        returncode=0,
        output="",
        summary=data,
    )


def test_records_a_step_with_frames() -> None:
    record = sweep.step_record(
        sweep.Step("bpsk-1/2", 5.0, 1),
        run(hardware.FINISHED, summary()),
        {"cpu0": 50.0},
    )
    assert record["complete"]
    assert record["overflows"] == 0
    assert record["lost"] == 2
    assert record["latency_us"] == {"p50": 5.0, "p99": 7.0}
    assert record["processing_us"]["frame_detector"] == {"p50": 1.0, "p99": 3.0}
    assert record["cpu_percent"] == {"cpu0": 50.0}


def test_a_stopped_steps_losses_are_what_it_didnt_receive() -> None:
    record = sweep.step_record(
        sweep.Step("bpsk-1/2", 5.0, 1),
        run(hardware.TIMED_OUT, summary(lost=None)),
        {},
    )
    assert not record["complete"]
    assert record["lost"] == 2


def test_an_idle_step_stopped_at_its_limit_is_complete() -> None:
    record = sweep.step_record(
        sweep.Step("idle", 5.0, 1), run(hardware.TIMED_OUT, summary()), {}
    )
    assert record["complete"]
    assert "lost" not in record
    assert "latency_us" not in record


def test_an_idle_step_that_received_nothing_is_incomplete() -> None:
    data = summary()
    data["metrics"]["arrival_interval"]["count"] = 0
    record = sweep.step_record(
        sweep.Step("idle", 5.0, 1), run(hardware.TIMED_OUT, data), {}
    )
    assert not record["complete"]


def test_an_idle_step_without_metrics_is_complete_once_stopped() -> None:
    data = summary()
    data["metrics"] = {"enabled": False, "overflows": 0}
    record = sweep.step_record(
        sweep.Step("idle", 5.0, 1), run(hardware.TIMED_OUT, data), {}
    )
    assert record["complete"]


def test_an_idle_step_that_exited_by_itself_is_incomplete() -> None:
    record = sweep.step_record(
        sweep.Step("idle", 5.0, 1), run(hardware.FINISHED, summary()), {}
    )
    assert not record["complete"]


def test_a_step_without_a_summary_is_incomplete() -> None:
    record = sweep.step_record(
        sweep.Step("idle", 5.0, 1), run(hardware.CRASHED, None), {}
    )
    assert not record["complete"]
    assert "overflows" not in record


def record(
    workload: str, bandwidth: float, overflows: int, complete: bool = True
) -> dict[str, Any]:
    return {
        "workload": workload,
        "bandwidth_mhz": bandwidth,
        "repetition": 1,
        "complete": complete,
        "overflows": overflows,
        "lost": 0,
        "latency_us": {"p50": 5.0, "p99": 7.0},
        "cpu_percent": {"cpu0": 40.0, "cpu1": 60.0},
    }


def test_finds_the_highest_bandwidth_each_workload_keeps_up_with() -> None:
    records = [
        record("bpsk-1/2", 5.0, 0),
        record("bpsk-1/2", 10.0, 0),
        record("bpsk-1/2", 15.0, 3),
        # Luck, after failing at 15 MHz
        record("bpsk-1/2", 20.0, 0),
        record("idle", 5.0, 0),
        record("idle", 10.0, 0, complete=False),
        record("qam64-3/4", 5.0, 1),
    ]
    assert sweep.highest_kept_up(records) == {
        "bpsk-1/2": 10.0,
        "idle": 5.0,
        "qam64-3/4": None,
    }


def test_one_repetition_with_overflows_doesnt_keep_up() -> None:
    assert not sweep.keeps_up([record("idle", 5.0, 0), record("idle", 5.0, 1)])


def test_runs_each_repetition_through_every_step() -> None:
    seen: list[str] = []

    def run_step(
        step: Any, args: Sequence[str], limit_s: float
    ) -> tuple[hardware.Run, dict[str, float]]:
        seen.append(f"{step.repetition} {step.bandwidth_mhz:g} {args[0]}")
        return run(hardware.FINISHED, summary()), {}

    saved: list[int] = []
    records = sweep.realtime(
        run_step,
        USRP,
        RADIO,
        ["bpsk-1/2", "idle"],
        [5.0, 10.0],
        2,
        1.0,
        lambda so_far: saved.append(len(so_far)),
    )
    assert seen == [
        "1 5 loopback",
        "1 5 rx",
        "1 10 loopback",
        "1 10 rx",
        "2 5 loopback",
        "2 5 rx",
        "2 10 loopback",
        "2 10 rx",
    ]
    assert len(records) == 8
    assert saved == [1, 2, 3, 4, 5, 6, 7, 8]


def test_overflows_are_unknown_without_a_summary() -> None:
    crashed = {**record("idle", 5.0, 0, complete=False)}
    del crashed["overflows"]
    line = sweep.realtime_table([crashed]).splitlines()[-1]
    assert line.split("|")[4].strip() == "?"


def test_writes_the_headline_and_a_row_per_step() -> None:
    table = sweep.realtime_table(
        [record("bpsk-1/2", 5.0, 0), record("bpsk-1/2", 20.0, 3)]
    )
    assert "| bpsk-1/2 | 5 MHz" in table.replace("  ", " ").replace("  ", " ")
    lines = [line for line in table.splitlines() if line.startswith("| bpsk")]
    assert len(lines) == 3
    assert lines[2].split("|")[4].strip() == "3"  # Overflows
    assert lines[2].split("|")[7].strip() == "60"  # Busiest CPU
    assert lines[2].split("|")[8].strip() == "no"


def test_frames_take_longer_at_lower_rates_and_bandwidths() -> None:
    # 1500 bytes and the CRC at BPSK 1/2: 503 symbols, 2032 µs at 20 MHz
    assert hardware.frame_seconds("bpsk-1/2", 20) == pytest.approx(2032e-6)
    assert hardware.frame_seconds("bpsk-1/2", 5) == pytest.approx(4 * 2032e-6)
    assert hardware.frame_seconds("qam64-3/4", 20) < 300e-6


def test_main_refuses_impossible_ports(tmp_path: pathlib.Path) -> None:
    binary = tmp_path / "fun-ofdm"
    binary.touch()
    with pytest.raises(SystemExit):
        sweep.main(
            ["realtime", "--fun-ofdm", str(binary), "--tx-port", "A:RX2"]
        )
