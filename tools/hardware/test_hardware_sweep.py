import dataclasses
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


def loopback_summary(
    rate: str, sent: int, lost: int, snr: float | None, count: int
) -> dict[str, Any]:
    return {
        "sent": {"packets": sent},
        "received": {"packets": sent - lost},
        "lost": lost,
        "metrics": {
            "frames": {
                "detected": sent,
                "sync_lost": lost,
                "signal_failed": 0,
                "crc_failed": {rate: 0},
            },
            "snr": {rate: {"count": count, "p50": snr}},
        },
    }


def test_records_a_per_step() -> None:
    step = sweep.per_step(
        "bpsk-1/2",
        70.0,
        500,
        run(
            hardware.FINISHED, loopback_summary("bpsk-1/2", 500, 50, 22.0, 450)
        ),
    )
    assert step.per == 0.1
    assert step.snr_estimate == 22.0
    assert step.estimates == 450
    assert step.sync_lost == 50


def test_a_per_step_without_a_summary_lost_every_frame() -> None:
    step = sweep.per_step("bpsk-1/2", 70.0, 100, run(hardware.CRASHED, None))
    assert step.per == 1.0
    assert (step.sent, step.lost) == (100, 100)
    assert step.snr_estimate is None
    assert not step.finished


def step(
    gain: float,
    lost: int,
    snr: float | None,
    estimates: int = 500,
    status: str = hardware.FINISHED,
) -> Any:
    return sweep.PerStep(
        rate="bpsk-1/2",
        tx_gain=gain,
        status=status,
        sent=500,
        lost=lost,
        snr_estimate=snr,
        estimates=estimates,
        detected=500,
        sync_lost=lost,
        signal_failed=0,
        crc_failed=0,
    )


def test_fits_snr_to_tx_gain_where_the_estimate_is_reliable() -> None:
    steps = [
        # Only a few frames got through, the louder ones: it reads high
        step(55, 480, 12.0, estimates=20),
        # Most frames got through, but most were lost
        step(58, 300, 11.0),
        step(60, 200, 12.0),
        step(61, 100, 13.0),
        step(62, 10, 14.0),
        step(63, 0, 30.0, status=hardware.CRASHED),
    ]
    fit = sweep.fit_snr(steps)
    assert fit is not None
    assert fit.offset_db == -48.0
    assert fit.slope == pytest.approx(1.0)
    assert fit.gains == (60, 61, 62)


def test_cant_fit_without_a_reliable_estimate() -> None:
    assert sweep.fit_snr([step(55, 500, None, estimates=0)]) is None


def test_makes_points_at_the_fitted_snrs() -> None:
    fit = sweep.Fit(offset_db=-48.0, slope=1.0, gains=(60.0,))
    points = sweep.points([step(61, 10, 13.0), step(60, 100, 12.0)], fit)
    assert [p.snr_db for p in points] == [12.0, 13.0]
    assert [p.lost for p in points] == [100, 10]


def test_a_failed_step_doesnt_move_the_crossing() -> None:
    fit = sweep.Fit(offset_db=-48.0, slope=1.0, gains=(60.0,))
    steps = [
        step(60, 150, None),
        step(61, 60, None),
        step(62, 500, None, status=hardware.CRASHED),
        step(63, 25, None),
    ]
    points = sweep.points(steps, fit)
    assert [p.snr_db for p in points] == [12.0, 13.0, 15.0]
    snr = sweep.per_sweep.snr_at_per(points, 0.1)
    # Between 13 dB (12%) and 15 dB (5%). With the failed step's 100%, it
    # would be past 14 dB.
    assert snr is not None
    assert 13 < snr < 14


def waterfall(rate: str, tx_gain: float, frames: int) -> Any:
    # A waterfall with 10% PER at TX gain 66, and SNR = TX gain - 48.
    per = min(1.0, max(0.0, 0.1 * 3 ** (66 - tx_gain)))
    lost = round(per * frames)
    return dataclasses.replace(
        step(tx_gain, lost, tx_gain - 48, estimates=frames - lost),
        rate=rate,
        sent=frames,
    )


def test_steps_across_the_waterfall_and_extends_it() -> None:
    gains: list[float] = []

    def run_step(rate: str, tx_gain: float, frames: int) -> Any:
        gains.append(tx_gain)
        return waterfall(rate, tx_gain, frames)

    saved: list[int] = []
    steps = sweep.per_rate(
        run_step, "bpsk-1/2", 70, lambda so_far: saved.append(len(so_far))
    )
    assert gains[:11] == list(range(65, 76))
    # Then down until the PER is at least 90%: 64 is 90%
    assert gains[11:] == [64]
    assert [s.tx_gain for s in steps] == list(range(64, 76))
    assert saved == list(range(1, 13))


def test_extends_the_waterfall_up_until_the_per_is_about_0() -> None:
    gains: list[float] = []

    def run_step(rate: str, tx_gain: float, frames: int) -> Any:
        gains.append(tx_gain)
        return waterfall(rate, tx_gain, frames)

    sweep.per_rate(run_step, "bpsk-1/2", 62)
    # 57 to 67, then up until the PER is at most 1%: at 69, 0.1 / 3³
    assert gains[11:] == [68, 69]


def test_stops_at_the_tx_gain_limit() -> None:
    gains: list[float] = []

    def run_step(rate: str, tx_gain: float, frames: int) -> Any:
        gains.append(tx_gain)
        return step(tx_gain, frames, None)  # Nothing gets through

    sweep.per_rate(run_step, "bpsk-1/2", 87)
    assert max(gains) == sweep.TX_GAIN_RANGE[1]
    assert gains == sorted(set(gains), key=gains.index)  # No step twice


def test_extends_past_a_failed_step_at_the_end() -> None:
    def run_step(rate: str, tx_gain: float, frames: int) -> Any:
        if tx_gain == 65:
            return step(65, frames, None, status=hardware.CRASHED)
        return waterfall(rate, tx_gain, frames)

    steps = sweep.per_rate(run_step, "bpsk-1/2", 70)
    # 65 failed, so its PER of 100% doesn't stop it: 64 is 90%
    assert steps[0].tx_gain == 64


def test_measures_each_rates_curve_and_implementation_loss() -> None:
    found = sweep.per(waterfall, ["bpsk-1/2"], {"bpsk-1/2": 17.0})
    curve = found["rates"]["bpsk-1/2"]
    assert curve["fit"].offset_db == -48.0
    assert curve["snr_at_target_per"] == pytest.approx(18.0)
    assert curve["implementation_loss_db"] == pytest.approx(1.0)
    assert found["calibration"].tx_gain == sweep.CALIBRATION_GAIN


def test_says_when_no_frame_gets_through() -> None:
    def nothing(rate: str, tx_gain: float, frames: int) -> Any:
        return step(tx_gain, frames, None, estimates=0)

    with pytest.raises(hardware.HardwareError, match="too few frames"):
        sweep.per(nothing, ["bpsk-1/2"], {"bpsk-1/2": 17.0})


def test_saves_after_each_step() -> None:
    saved: list[tuple[list[str], int]] = []
    found = sweep.per(
        waterfall,
        ["bpsk-1/2", "qpsk-1/2"],
        {"bpsk-1/2": 17.0, "qpsk-1/2": 17.0},
        lambda found: saved.append(
            (
                list(found["rates"]),
                sum(len(c["steps"]) for c in found["rates"].values()),
            )
        ),
    )
    steps = sum(len(c["steps"]) for c in found["rates"].values())
    # The calibration, each step, then each rate when it's done
    assert saved[0] == ([], 0)
    assert saved[1] == (["bpsk-1/2"], 1)
    assert saved[-1] == (["bpsk-1/2", "qpsk-1/2"], steps)
    assert len(saved) == 1 + steps + 2


def test_writes_the_curves_and_a_table() -> None:
    found = sweep.per(waterfall, ["bpsk-1/2"], {"bpsk-1/2": 17.0})
    data = sweep.per_json(found)
    steps = data["rates"]["bpsk-1/2"]["steps"]
    assert steps[0]["snr_db"] == steps[0]["tx_gain"] - 48
    assert steps[0]["per_low"] <= steps[0]["per"] <= steps[0]["per_high"]
    assert data["rates"]["bpsk-1/2"]["fit"]["offset_db"] == -48.0
    table = sweep.per_table(found)
    assert "| bpsk-1/2 | 18.0 dB" in table
    assert "+1.0 dB" in table


def test_reads_the_simulated_snrs() -> None:
    sim = {"rates": {"bpsk-1/2": {"snr_at_target_per": 18.1}}}
    assert sweep.simulated_snrs(sim) == {"bpsk-1/2": 18.1}


def test_per_needs_the_simulated_curves(tmp_path: pathlib.Path) -> None:
    binary = tmp_path / "fun-ofdm"
    binary.touch()
    with pytest.raises(SystemExit):
        sweep.main(["per", "--fun-ofdm", str(binary)])


def test_flags_a_fit_whose_slope_isnt_1() -> None:
    found = sweep.per(waterfall, ["bpsk-1/2"], {"bpsk-1/2": 17.0})
    assert "check the fit" not in sweep.per_table(found)
    fit = found["rates"]["bpsk-1/2"]["fit"]
    found["rates"]["bpsk-1/2"]["fit"] = dataclasses.replace(fit, slope=0.8)
    assert "slope 0.80 (not 1: check the fit)" in sweep.per_table(found)


SIM = {
    "provenance": {"tool": "tools/per/sweep.py"},
    "payload_size": 1000,
    "target_per": 0.1,
    "rates": {},
}


def test_takes_a_simulated_sweep_with_the_same_frames() -> None:
    assert sweep.sim_problem(SIM) is None
    assert "isn't a tools/per/sweep.py" in str(
        sweep.sim_problem({**SIM, "provenance": {"tool": "other"}})
    )
    assert "payload size" in str(
        sweep.sim_problem({**SIM, "payload_size": 1500})
    )
    assert "target PER" in str(sweep.sim_problem({**SIM, "target_per": 0.01}))


def test_pers_radio_settings_have_its_bandwidth_and_no_tx_gain() -> None:
    namespace = sweep.argparse.Namespace(
        sweep="per",
        freq=None,
        channel=40,
        rx_gain=50.0,
        tx_port="B:TX/RX",
        rx_port="B:RX2",
    )
    assert sweep.radio_settings(namespace) == {
        "channel": 40,
        "rx_gain": 50.0,
        "tx_port": "B:TX/RX",
        "rx_port": "B:RX2",
        "bandwidth_mhz": sweep.PER_BANDWIDTH_MHZ,
    }


def test_stops_a_rate_whose_steps_all_fail() -> None:
    gains: list[float] = []

    def failing(rate: str, tx_gain: float, frames: int) -> Any:
        gains.append(tx_gain)
        return step(tx_gain, frames, None, status=hardware.CRASHED)

    sweep.per_rate(failing, "bpsk-1/2", 70)
    assert len(gains) == 2 * sweep.PER_HALF_WIDTH_DB + 1


def test_leaves_a_step_that_sent_nothing_out_of_the_curve() -> None:
    fit = sweep.Fit(offset_db=-48.0, slope=1.0, gains=(60.0,))
    empty = dataclasses.replace(step(62, 0, None), sent=0)
    assert [
        p.snr_db for p in sweep.points([step(61, 10, 13.0), empty], fit)
    ] == [13.0]


def test_needs_a_reliable_calibration() -> None:
    def mostly_lost(rate: str, tx_gain: float, frames: int) -> Any:
        return step(tx_gain, frames - 1, 30.0, estimates=1)

    with pytest.raises(hardware.HardwareError, match="too few frames"):
        sweep.per(mostly_lost, ["bpsk-1/2"], {"bpsk-1/2": 17.0})
