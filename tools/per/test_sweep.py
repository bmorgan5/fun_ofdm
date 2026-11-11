import json
import pathlib
import subprocess
import sys
import threading

import pytest
import sweep


def point(snr_db: float, lost: int, sent: int = 100) -> sweep.Point:
    return sweep.Point(
        snr_db=snr_db,
        sent=sent,
        lost=lost,
        detected=sent,
        sync_lost=0,
        signal_failed=0,
        crc_failed=lost,
    )


# Known values: Wilson's interval for 0, 50 and 100 failures in 100.
@pytest.mark.parametrize(
    ("failures", "low", "high"),
    [(0, 0.0, 0.0370), (50, 0.4038, 0.5962), (100, 0.9630, 1.0)],
)
def test_wilson_interval_matches_known_values(
    failures: int, low: float, high: float
) -> None:
    assert sweep.wilson_interval(failures, 100) == pytest.approx(
        (low, high), abs=1e-4
    )


def test_wilson_interval_narrows_with_more_trials() -> None:
    small = sweep.wilson_interval(10, 100)
    large = sweep.wilson_interval(100, 1000)
    assert large[1] - large[0] < small[1] - small[0]


def test_point_per_is_the_fraction_lost() -> None:
    assert point(10.0, lost=25).per == 0.25


def test_snr_at_per_is_the_point_that_hits_it_exactly() -> None:
    assert sweep.snr_at_per([point(10, 100), point(12, 10)], 0.1) == 12


# log10(PER) goes from 0 to -2, so it crosses -1 halfway.
def test_snr_at_per_interpolates_log_per() -> None:
    assert sweep.snr_at_per(
        [point(10, 100), point(12, 1)], 0.1
    ) == pytest.approx(11)


def test_snr_at_per_takes_the_first_crossing() -> None:
    curve = [point(10, 50), point(11, 5), point(12, 50), point(13, 0)]
    assert sweep.snr_at_per(curve, 0.1) == pytest.approx(
        10 + (-1 - (-0.30103)) / (-1.30103 - (-0.30103))
    )


# With no losses, the point counts as a tenth of a frame lost: PER 0.001.
def test_snr_at_per_treats_no_losses_as_a_tenth_of_a_frame() -> None:
    assert sweep.snr_at_per(
        [point(10, 100), point(12, 0)], 0.1
    ) == pytest.approx(10 + 2 / 3)


def test_snr_at_per_is_none_if_never_reached() -> None:
    assert sweep.snr_at_per([point(10, 100), point(12, 50)], 0.1) is None


def test_snr_at_per_is_none_if_the_curve_starts_below() -> None:
    assert sweep.snr_at_per([point(10, 5), point(12, 0)], 0.1) is None


def test_a_falling_curve_is_monotonic() -> None:
    assert sweep.is_monotonic([point(10, 90), point(11, 40), point(12, 0)])


# 40 and 44 in 100 are well within chance of each other.
def test_a_rise_within_chance_is_still_monotonic() -> None:
    assert sweep.is_monotonic([point(10, 90), point(11, 40), point(12, 44)])


def test_a_rise_beyond_chance_isnt_monotonic() -> None:
    assert not sweep.is_monotonic([point(10, 90), point(11, 5), point(12, 60)])


def test_snr_grid_includes_both_ends() -> None:
    assert sweep.snr_grid(14, 16, 0.5) == [14, 14.5, 15, 15.5, 16]


# 0.1 doesn't add up exactly in floating point.
def test_snr_grid_reaches_the_end_despite_rounding() -> None:
    assert sweep.snr_grid(0, 1, 0.1)[-1] == 1


def test_sweep_runs_every_rate_at_every_snr_in_order() -> None:
    calls = []
    lock = threading.Lock()

    def run(rate: str, snr: float, frames: int) -> sweep.Point:
        with lock:
            calls.append((rate, snr, frames))
        return point(snr, lost=int(snr))

    curves = sweep.sweep(run, ["bpsk-1/2", "qam64-3/4"], [1, 2, 3], 100, 4)
    assert sorted(calls) == [
        (rate, snr, 100)
        for rate in ["bpsk-1/2", "qam64-3/4"]
        for snr in [1, 2, 3]
    ]
    assert list(curves) == ["bpsk-1/2", "qam64-3/4"]
    for points in curves.values():
        assert [p.snr_db for p in points] == [1, 2, 3]


def test_sweep_reports_each_point_as_it_finishes() -> None:
    reported = []
    lock = threading.Lock()

    def on_point(rate: str, result: sweep.Point) -> None:
        with lock:
            reported.append((rate, result.snr_db))

    sweep.sweep(
        lambda rate, snr, frames: point(snr, lost=0),
        ["bpsk-1/2", "qam64-3/4"],
        [1, 2],
        100,
        4,
        on_point,
    )
    assert sorted(reported) == [
        ("bpsk-1/2", 1),
        ("bpsk-1/2", 2),
        ("qam64-3/4", 1),
        ("qam64-3/4", 2),
    ]


def test_progress_printer_counts_the_points_done(
    capsys: pytest.CaptureFixture[str],
) -> None:
    on_point = sweep.progress_printer(2)
    on_point("bpsk-1/2", point(10, lost=3))
    on_point("bpsk-1/2", point(11, lost=0))
    lines = capsys.readouterr().err.splitlines()
    assert lines[0].startswith("[1/2] bpsk-1/2 at 10 dB: 3/100 lost")
    assert lines[1].startswith("[2/2] bpsk-1/2 at 11 dB: 0/100 lost")


# A stand-in for fun-ofdm: it writes sim's JSON and a line to stderr, writes
# its arguments to the file in FAKE_ARGS if it's set, and exits with the status
# in FAKE_STATUS.
FAKE_FUN_OFDM = """\
import json, os, sys
if "FAKE_ARGS" in os.environ:
    with open(os.environ["FAKE_ARGS"], "w") as f:
        json.dump(sys.argv[1:], f)
print(json.dumps({
    "lost": 7,
    "metrics": {"frames": {
        "detected": 101, "sync_lost": 3, "signal_failed": 1,
        "crc_failed": {"qpsk-1/2": 4, "bpsk-1/2": 99},
    }},
}))
print("fake fun-ofdm's stderr", file=sys.stderr)
sys.exit(int(os.environ.get("FAKE_STATUS", "1")))
"""


@pytest.fixture
def fake_fun_ofdm(tmp_path: pathlib.Path) -> pathlib.Path:
    path = tmp_path / "fun-ofdm"
    path.write_text(f"#!{sys.executable}\n{FAKE_FUN_OFDM}")
    path.chmod(0o755)
    return path


def test_sim_runner_reads_sims_summary(fake_fun_ofdm: pathlib.Path) -> None:
    result = sweep.sim_runner(fake_fun_ofdm, seed=5)("qpsk-1/2", 12.5, 100)
    assert result == sweep.Point(
        snr_db=12.5,
        sent=100,
        lost=7,
        detected=101,
        sync_lost=3,
        signal_failed=1,
        crc_failed=4,
    )


def test_sim_runner_runs_sim_with_the_points_settings(
    fake_fun_ofdm: pathlib.Path,
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    args = tmp_path / "args.json"
    monkeypatch.setenv("FAKE_ARGS", str(args))
    sweep.sim_runner(fake_fun_ofdm, seed=5)("qpsk-1/2", 12.5, 100)
    assert json.loads(args.read_text()) == [
        "sim",
        "--phy-rate",
        "qpsk-1/2",
        "--snr",
        "12.5",
        "--packets",
        "100",
        "--payload-size",
        "1000",
        "--seed",
        "5",
        "--json",
    ]


@pytest.mark.parametrize("status", [0, 1])
def test_sim_runner_accepts_sim_passing_or_losing_frames(
    fake_fun_ofdm: pathlib.Path, monkeypatch: pytest.MonkeyPatch, status: int
) -> None:
    monkeypatch.setenv("FAKE_STATUS", str(status))
    assert sweep.sim_runner(fake_fun_ofdm, 1)("qpsk-1/2", 10, 100).lost == 7


def test_sim_runner_raises_with_sims_stderr_if_sim_fails(
    fake_fun_ofdm: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("FAKE_STATUS", "2")
    with pytest.raises(subprocess.CalledProcessError) as error:
        sweep.sim_runner(fake_fun_ofdm, 1)("qpsk-1/2", 10, 100)
    assert "fake fun-ofdm's stderr" in "\n".join(error.value.__notes__)


def test_to_json_has_each_rates_curve_and_s6() -> None:
    curves = {"bpsk-1/2": [point(10, 100), point(12, 10)]}
    result = sweep.to_json(curves, frames=100, seed=1)
    assert result["payload_size"] == 1000
    rate = result["rates"]["bpsk-1/2"]
    assert rate["snr_at_target_per"] == 12
    assert rate["monotonic"] is True
    assert rate["points"][1]["per"] == 0.1
    assert rate["points"][1]["per_low"] < 0.1 < rate["points"][1]["per_high"]
    json.dumps(result)  # Serializable


def test_table_has_a_row_per_rate() -> None:
    curves = {
        "bpsk-1/2": [point(10, 100), point(12, 10)],
        "qam64-3/4": [point(10, 100), point(12, 100)],
    }
    assert sweep.table(curves).splitlines()[2:] == [
        "| bpsk-1/2 | 12.0 dB | yes |",
        "| qam64-3/4 | not reached | yes |",
    ]


def test_main_writes_the_sweep(
    fake_fun_ofdm: pathlib.Path,
    tmp_path: pathlib.Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    out = tmp_path / "per.json"
    status = sweep.main(
        [
            "--fun-ofdm",
            str(fake_fun_ofdm),
            "--rates",
            "qpsk-1/2",
            "--snr-min",
            "10",
            "--snr-max",
            "11",
            "--frames",
            "100",
            "--out",
            str(out),
        ]
    )
    assert status == 0
    assert "| qpsk-1/2 |" in capsys.readouterr().out
    points = json.loads(out.read_text())["rates"]["qpsk-1/2"]["points"]
    assert [p["snr_db"] for p in points] == [10, 11]


def test_main_needs_the_binary(tmp_path: pathlib.Path) -> None:
    with pytest.raises(SystemExit):
        sweep.main(["--fun-ofdm", str(tmp_path / "missing")])
