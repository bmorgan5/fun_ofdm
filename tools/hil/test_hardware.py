import json
import pathlib
from collections.abc import Sequence
from typing import Any

import hardware

# uhd_find_devices with one B210 (UHD 4.9).
ONE_B210 = """\
[INFO] [UHD] linux; GNU C++ version 15.2.0; Boost_109000; UHD_4.9.0.1-1ubuntu1
--------------------------------------------------
-- UHD Device 0
--------------------------------------------------
Device Address:
    serial: F571FA
    name:
    product: B210
    type: b200

"""

TWO_DEVICES = (
    ONE_B210
    + """\
--------------------------------------------------
-- UHD Device 1
--------------------------------------------------
Device Address:
    serial: F61912
    name: MyB200
    product: B200
    type: b200
"""
)

NO_DEVICES = """\
[INFO] [UHD] linux; GNU C++ version 15.2.0; Boost_109000; UHD_4.9.0.1-1ubuntu1
No UHD Devices Found
"""

# The parts of a `fun-ofdm loopback --json` summary that loopback_row reads.
SUMMARY: dict[str, Any] = {
    "options": {"phy_rate": "bpsk-1/2"},
    "sent": {"packets": 100},
    "received": {"packets": 98, "bits_per_second": 1296000.0},
    "lost": 2,
    "duplicates": 0,
    "mismatched_bytes": 0,
    "metrics": {
        "overflows": 1,
        "frames": {"sync_lost": 37},
        "arrival_interval": {
            "count": 1200,
            "p50": 815103,
            "p99": 1148927,
            "max": 1775615,
        },
    },
}


def test_parses_a_b210() -> None:
    assert hardware.parse_find_devices(ONE_B210) == [
        hardware.Usrp(serial="F571FA", product="B210")
    ]


def test_parses_every_device_in_order() -> None:
    assert [u.serial for u in hardware.parse_find_devices(TWO_DEVICES)] == [
        "F571FA",
        "F61912",
    ]


def test_finds_no_devices() -> None:
    assert hardware.parse_find_devices(NO_DEVICES) == []


def add_usb_device(sysfs: pathlib.Path, name: str, vendor: str) -> None:
    device = sysfs / "bus" / "usb" / "devices" / name
    device.mkdir(parents=True)
    (device / "idVendor").write_text(vendor + "\n")


def test_counts_the_usrps_on_usb(tmp_path: pathlib.Path) -> None:
    add_usb_device(tmp_path, "2-1", "2500")  # A B210
    add_usb_device(tmp_path, "1-1", "046d")  # A webcam
    (tmp_path / "bus" / "usb" / "devices" / "1-1:1.0").mkdir()  # An interface
    assert hardware.usrps_on_usb(tmp_path) == 1


def test_counts_no_usrps_without_usb(tmp_path: pathlib.Path) -> None:
    assert hardware.usrps_on_usb(tmp_path) == 0


def test_reads_the_usb_version() -> None:
    assert hardware.usb_version("[INFO] [B200] Operating over USB 3.") == 3
    assert hardware.usb_version("[INFO] [B200] Operating over USB 2.") == 2


def test_has_no_usb_version_without_the_line() -> None:
    assert hardware.usb_version("Error: no devices found") is None


def test_reads_the_uhd_version() -> None:
    assert hardware.uhd_version("UHD 4.9.0.1-1ubuntu1\n") == "4.9.0.1-1ubuntu1"
    assert hardware.uhd_version(None) is None


def test_every_topology_has_a_diagram_and_steps() -> None:
    for name, topology in hardware.TOPOLOGIES.items():
        assert topology.name == name
        assert topology.diagram
        assert topology.steps


def test_makes_a_row_from_a_loopback_summary() -> None:
    assert hardware.loopback_row(SUMMARY) == {
        "Rate": "bpsk-1/2",
        "Sent": "100",
        "Received": "98",
        "Lost": "2",
        "Duplicates": "0",
        "Bytes wrong": "0",
        "Overflows": "1",
        "False detections": "37",
        "Sample arrival p50 / p99 / max (µs)": "815 / 1149 / 1776",
        "Mbit/s": "1.30",
    }


def test_marks_a_stopped_runs_missing_check() -> None:
    summary = {
        key: value
        for key, value in SUMMARY.items()
        if key not in ("lost", "duplicates", "mismatched_bytes")
    }
    row = hardware.loopback_row(summary)
    assert (row["Lost"], row["Duplicates"], row["Bytes wrong"]) == ("?",) * 3


def test_streams_when_batches_arrive_at_the_sample_rate() -> None:
    assert hardware.streaming(SUMMARY)


def test_doesnt_stream_when_every_batch_times_out() -> None:
    # A wedged B210: each batch took UHD's 100 ms timeout.
    arrival = {"count": 8, "p50": 100336127, "p99": 101253119, "max": 101253119}
    assert not hardware.streaming({"metrics": {"arrival_interval": arrival}})


def test_doesnt_stream_without_batches() -> None:
    arrival = {"count": 0, "p50": 0, "p99": 0, "max": 0}
    assert not hardware.streaming({"metrics": {"arrival_interval": arrival}})


def test_writes_a_markdown_table() -> None:
    rows = [{"A": "1", "B": "2"}, {"A": "3", "B": "4"}]
    assert hardware.markdown_table(rows) == (
        "| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n"
    )


def test_writes_no_table_without_rows() -> None:
    assert hardware.markdown_table([]) == ""


def fake_run(outputs: dict[str, str]) -> hardware.Runner:
    # Answers each command by its program's name, or fails.
    def run(command: Sequence[str]) -> str | None:
        return outputs.get(pathlib.Path(command[0]).name)

    return run


def test_records_the_metadata(tmp_path: pathlib.Path) -> None:
    run = fake_run(
        {"uhd_config_info": "UHD 4.9.0.1-1ubuntu1\n", "git": "abc123\n"}
    )
    meta = hardware.metadata(
        tmp_path / "fun-ofdm",
        [hardware.Usrp(serial="F571FA", product="B210")],
        {"freq_ghz": 5.18, "atten_db": 40},
        run,
    )
    assert meta["uhd_version"] == "4.9.0.1-1ubuntu1"
    assert meta["usrps"] == [{"serial": "F571FA", "product": "B210"}]
    assert meta["settings"] == {"freq_ghz": 5.18, "atten_db": 40}
    assert meta["host"]["commit"] == "abc123"


def test_writes_the_results(tmp_path: pathlib.Path) -> None:
    meta = {
        "host": {"commit": "0123456789abcdef", "dirty": True},
        "uhd_version": "4.9.0.1",
        "usrps": [{"serial": "F571FA", "product": "B210"}],
        "settings": {"freq_ghz": 5.18, "atten_db": 40},
    }
    out = tmp_path / "link_smoke"
    hardware.write_results(
        out, "Link smoke test", meta, {"bpsk-1/2": SUMMARY}, [{"A": "1"}]
    )
    assert json.loads((out / "result.json").read_text()) == {
        **meta,
        "runs": {"bpsk-1/2": SUMMARY},
    }
    assert (out / "result.md").read_text() == (
        "# Link smoke test\n\n"
        "B210 F571FA; freq_ghz 5.18, atten_db 40; UHD 4.9.0.1; "
        "commit 0123456789ab (with uncommitted changes)\n\n"
        "| A |\n|---|\n| 1 |\n"
    )
