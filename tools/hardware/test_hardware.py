import json
import logging
import os
import pathlib
import re
import time
from collections.abc import Sequence
from typing import Any

import hardware
import pytest

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


def test_a_b210_has_four_ports_two_of_which_transmit() -> None:
    assert list(hardware.PORTS) == ["A:TX/RX", "A:RX2", "B:TX/RX", "B:RX2"]
    assert [str(p) for p in hardware.PORTS.values() if p.can_transmit] == [
        "RF A TX/RX",
        "RF B TX/RX",
    ]


def test_turns_ports_into_fun_ofdms_options() -> None:
    tx, rx = hardware.PORTS["B:TX/RX"], hardware.PORTS["A:RX2"]
    assert hardware.port_args(tx, rx) == [
        "--tx-subdev=A:B",
        "--tx-antenna=TX/RX",
        "--rx-subdev=A:A",
        "--rx-antenna=RX2",
    ]


def test_allows_a_receiver_on_the_transmitters_channel() -> None:
    ports = hardware.PORTS
    assert hardware.port_problem(ports["A:TX/RX"], ports["A:RX2"]) is None


def test_refuses_impossible_ports() -> None:
    ports = hardware.PORTS
    assert "can't transmit" in (
        hardware.port_problem(ports["B:RX2"], ports["A:TX/RX"]) or ""
    )
    assert "both" in (
        hardware.port_problem(ports["A:TX/RX"], ports["A:TX/RX"]) or ""
    )


def test_makes_a_row_from_a_loopback_summary() -> None:
    assert hardware.loopback_row(SUMMARY) == {
        "Sent": "100",
        "Received": "98",
        "Lost": "2",
        "Duplicates": "0",
        "Bytes wrong": "0",
        "Overflows": "1",
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


def test_marks_every_column_without_a_summary() -> None:
    row = hardware.loopback_row(None)
    assert tuple(row) == hardware.LOOPBACK_COLUMNS
    assert set(row.values()) == {"?"}


def test_has_the_loopback_columns() -> None:
    assert tuple(hardware.loopback_row(SUMMARY)) == hardware.LOOPBACK_COLUMNS


def make_run(**changes: Any) -> hardware.Run:
    # A run that finished, with changes.
    fields: dict[str, Any] = {
        "name": "bpsk-1/2",
        "status": hardware.FINISHED,
        "seconds": 12.4,
        "limit_s": 230,
        "returncode": 0,
        "output": "Looping back\nPASS\n",
        "summary": SUMMARY,
    }
    return hardware.Run(**{**fields, **changes})


def test_a_runs_log_is_named_after_it() -> None:
    assert make_run().log_name == "bpsk-1-2.log"


def test_a_runs_row_says_how_it_ended() -> None:
    row = hardware.run_row(make_run(status=hardware.TIMED_OUT, summary=None))
    assert list(row)[:3] == ["Run", "Status", "Time (s)"]
    assert (row["Run"], row["Status"], row["Time (s)"]) == (
        "bpsk-1/2",
        "timed out",
        "12",
    )
    assert row["Received"] == "?"


def test_a_runs_log_is_its_output() -> None:
    assert hardware.run_log(make_run()) == "Looping back\nPASS\n"


def test_a_timed_out_runs_log_ends_with_its_threads() -> None:
    run = make_run(status=hardware.TIMED_OUT, threads="7 fun-ofdm: sleeping\n")
    assert hardware.run_log(run) == (
        "Looping back\nPASS\n"
        "\n--- Threads, at the 230 s time limit ---\n"
        "7 fun-ofdm: sleeping\n"
    )


def test_explains_a_timeout() -> None:
    run = make_run(status=hardware.TIMED_OUT)
    text = hardware.explain(run, pathlib.Path("out/bpsk-1-2.log"))
    assert text.startswith(
        "bpsk-1/2 didn't finish within 230 s, so it was stopped."
    )
    assert "Log: out/bpsk-1-2.log\n" in text
    assert text.endswith("  Looping back\n  PASS\n")


def test_explains_where_to_find_a_log_that_wasnt_written() -> None:
    text = hardware.explain(make_run(status=hardware.TIMED_OUT), None)
    assert (
        "stopped. Where each thread was waiting is at the end of its log."
        in text
    )
    assert "Pass --out to keep its whole log\n" in text


def test_explains_a_crash() -> None:
    run = make_run(status=hardware.CRASHED, returncode=1, summary=None)
    text = hardware.explain(run, pathlib.Path("x.log"))
    assert text.startswith(
        "bpsk-1/2 exited with status 1 after 12 s, without writing a summary."
    )


def test_explains_with_only_the_end_of_the_output() -> None:
    output = "".join(f"line {n}\n" for n in range(100))
    text = hardware.explain(make_run(output=output), pathlib.Path("x.log"))
    lines = text.splitlines()
    assert len(lines) == 1 + hardware.TAIL_LINES
    assert lines[-1] == "  line 99"


def fake_proc(files: dict[str, str]) -> hardware.Runner:
    # /proc as a dict of paths: `cat` reads one, and `ls` lists a directory.
    def run(command: Sequence[str]) -> str | None:
        program, path = command
        if program == "cat":
            return files.get(path)
        children = {
            name.split("/")[0]
            for full in files
            if full.startswith(path + "/")
            for name in [full.removeprefix(path + "/")]
        }
        return "\n".join(sorted(children)) + "\n" if children else None

    return run


def test_finds_a_process_under_sudo() -> None:
    proc = fake_proc(
        {
            "/proc/10/comm": "sudo\n",
            "/proc/10/task/10/children": "11 ",
            "/proc/11/comm": "sudo\n",
            "/proc/11/task/11/children": "12 ",
            "/proc/12/comm": "fun-ofdm\n",
        }
    )
    assert hardware.find_process(10, "fun-ofdm", proc) == 12


def test_finds_a_process_by_its_truncated_name() -> None:
    proc = fake_proc({"/proc/10/comm": "a-very-long-nam\n"})
    assert hardware.find_process(10, "a-very-long-name", proc) == 10


def test_doesnt_find_a_process_that_isnt_there() -> None:
    proc = fake_proc({"/proc/10/comm": "sudo\n"})
    assert hardware.find_process(10, "fun-ofdm", proc) is None


def test_reads_where_each_thread_is_waiting() -> None:
    proc = fake_proc(
        {
            "/proc/12/task/12/comm": "fun-ofdm\n",
            "/proc/12/task/12/stat": "12 (fun-ofdm) S 11 12",
            "/proc/12/task/12/wchan": "futex_wait_queue",
            "/proc/12/task/12/stack": "[<0>] futex_wait_queue+0x6c/0xa0\n",
            "/proc/12/task/13/comm": "a (b) c\n",
            "/proc/12/task/13/stat": "13 (a (b) c) R 11 12",
            "/proc/12/task/13/wchan": "0",
        }
    )
    assert hardware.thread_states(12, proc) == (
        "12 fun-ofdm: sleeping, waiting in futex_wait_queue\n"
        "    [<0>] futex_wait_queue+0x6c/0xa0\n"
        "13 a (b) c: running\n"
    )


def test_says_when_the_threads_cant_be_read() -> None:
    assert "can't be read" in hardware.thread_states(12, fake_proc({}))


def test_writes_a_markdown_table_with_its_columns_lined_up() -> None:
    rows = [
        {"Run": "bpsk-1/2", "Lost": "3"},
        {"Run": "qam16-1/2", "Lost": "12"},
    ]
    assert hardware.markdown_table(rows) == (
        "| Run       | Lost |\n"
        "| --------- | ---- |\n"
        "| bpsk-1/2  | 3    |\n"
        "| qam16-1/2 | 12   |\n"
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
        {"freq_ghz": 5.18, "bandwidth_mhz": 5},
        run,
    )
    assert meta["uhd_version"] == "4.9.0.1-1ubuntu1"
    assert meta["usrps"] == [{"serial": "F571FA", "product": "B210"}]
    assert meta["settings"] == {"freq_ghz": 5.18, "bandwidth_mhz": 5}
    assert meta["host"]["commit"] == "abc123"


META = {
    "host": {"commit": "0123456789abcdef", "dirty": True},
    "uhd_version": "4.9.0.1",
    "usrps": [{"serial": "F571FA", "product": "B210"}],
    "settings": {"freq_ghz": 5.18, "bandwidth_mhz": 5},
}


def test_makes_result_md() -> None:
    clean = {**META, "host": {"commit": "0123456789abcdef", "dirty": False}}
    assert hardware.results_markdown(
        "Link smoke test", clean, [{"A": "1"}]
    ) == (
        "# Link smoke test\n\n"
        "B210 F571FA; freq_ghz 5.18, bandwidth_mhz 5; UHD 4.9.0.1; "
        "commit 0123456789ab\n\n"
        "| A |\n| - |\n| 1 |\n"
    )


def test_writes_the_results(tmp_path: pathlib.Path) -> None:
    meta = META
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
        "B210 F571FA; freq_ghz 5.18, bandwidth_mhz 5; UHD 4.9.0.1; "
        "commit 0123456789ab (with uncommitted changes)\n\n"
        "| A |\n| - |\n| 1 |\n"
    )


# Fake commands. uhd_find_devices finds a B210 only as root, which the fake
# sudo pretends to be. `sudo -n true` exits with $SUDO_EXIT: 1 means sudo
# needs a password.
FIND_DEVICES = """\
#!/bin/sh
if [ "$FAKE_ROOT" = 1 ]; then
  printf -- '-- UHD Device 0\\n    serial: F571FA\\n    product: B210\\n'
else
  echo "[ERROR] [USB] USB open failed: insufficient permissions." >&2
fi
"""

SUDO = """\
#!/bin/sh
shift  # -n
[ "$1" = true ] && exit "$SUDO_EXIT"
FAKE_ROOT=1 exec "$@"
"""

PROBE = """\
#!/bin/sh
echo "Operating over USB $USB."
"""


def fake_uhd(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    sudo_exit: int,
    root: bool = False,
    usb: int = 3,
) -> None:
    # UHD's tools and sudo, first on the PATH, and one Ettus device on USB.
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    for name, script in (
        ("uhd_find_devices", FIND_DEVICES),
        ("uhd_usrp_probe", PROBE),
        ("sudo", SUDO),
    ):
        (bin_dir / name).write_text(script)
        (bin_dir / name).chmod(0o755)
    monkeypatch.setenv("PATH", f"{bin_dir}{os.pathsep}{os.environ['PATH']}")
    monkeypatch.setenv("SUDO_EXIT", str(sudo_exit))
    monkeypatch.setenv("FAKE_ROOT", "1" if root else "0")
    monkeypatch.setenv("USB", str(usb))
    monkeypatch.setattr(hardware, "usrps_on_usb", lambda: 1)


def test_runs_as_you_when_uhd_can_open_the_usrp(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fake_uhd(tmp_path, monkeypatch, sudo_exit=1, root=True)
    run = hardware.Commands()
    assert hardware.find_usrp(run) == hardware.Usrp("F571FA", "B210")
    assert not run.sudo


def test_uses_sudo_when_uhd_cant_open_the_usrp_as_you(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fake_uhd(tmp_path, monkeypatch, sudo_exit=0)
    run = hardware.Commands()
    assert hardware.find_usrp(run).serial == "F571FA"
    assert run.sudo


def test_says_to_run_sudo_v_when_sudo_needs_a_password(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fake_uhd(tmp_path, monkeypatch, sudo_exit=1)
    with pytest.raises(hardware.HardwareError, match="run `sudo -v` first"):
        hardware.find_usrp(hardware.Commands())


def test_refuses_a_usrp_on_usb_2(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fake_uhd(tmp_path, monkeypatch, sudo_exit=1, root=True, usb=2)
    with pytest.raises(hardware.HardwareError, match="on USB 2, not USB 3"):
        hardware.find_usrp(hardware.Commands())


def test_says_when_uhd_hangs(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fake_uhd(tmp_path, monkeypatch, sudo_exit=1, root=True)
    (tmp_path / "bin" / "uhd_find_devices").write_text("#!/bin/sh\nsleep 10\n")
    monkeypatch.setattr(hardware, "UHD_TIMEOUT_S", 0.2)
    with pytest.raises(
        hardware.HardwareError, match="uhd_find_devices didn't finish"
    ):
        hardware.find_usrp(hardware.Commands())


def test_a_command_that_hangs_has_no_stdout(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(hardware, "UHD_TIMEOUT_S", 0.2)
    assert hardware.Commands().stdout(["sleep", "10"]) is None


# Fake fun-ofdm binaries. Each writes a line, and the last argument is the
# --json summary's path.
FINISHES = """\
#!/bin/sh
for last; do :; done
echo "Looping back"
echo '{"sent": {"packets": 3}}' > "$last"
exit 1
"""

CRASHES = """\
#!/bin/sh
echo "Error: no USRP"
exit 3
"""

# Ignores Ctrl+C, so it has to be killed.
HANGS = """\
#!/bin/sh
trap '' INT
echo "Hanging"
while :; do sleep 0.1; done
"""


def run_fake_fun_ofdm(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    script: str,
) -> hardware.Run:
    # One run of a fake fun-ofdm, with a 1 s limit.
    monkeypatch.setattr(hardware, "KILL_AFTER_S", 0.5)
    monkeypatch.setattr(hardware, "HEARTBEAT_S", 0.3)
    fun_ofdm = tmp_path / "fun-ofdm"
    fun_ofdm.write_text(script)
    fun_ofdm.chmod(0o755)
    return hardware.run_fun_ofdm(
        hardware.Commands(), fun_ofdm, "bpsk-1/2", ["loopback"], 1
    )


def test_records_a_run_that_finishes(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    caplog: pytest.LogCaptureFixture,
) -> None:
    caplog.set_level(logging.INFO)
    run = run_fake_fun_ofdm(tmp_path, monkeypatch, FINISHES)
    # It finished even though it exited with 1, which is a failed check
    assert run.status == hardware.FINISHED
    assert run.returncode == 1
    assert run.summary == {"sent": {"packets": 3}}
    assert run.output == "Looping back\n"
    assert run.threads == ""
    assert "bpsk-1/2: finished after" in caplog.text


def test_records_a_run_that_crashes(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    caplog: pytest.LogCaptureFixture,
) -> None:
    caplog.set_level(logging.INFO)
    run = run_fake_fun_ofdm(tmp_path, monkeypatch, CRASHES)
    assert run.status == hardware.CRASHED
    assert run.returncode == 3
    assert run.summary is None
    assert run.output == "Error: no USRP\n"
    assert "bpsk-1/2: crashed with status 3 after" in caplog.text


def test_stops_a_run_that_hangs_and_says_where(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    caplog: pytest.LogCaptureFixture,
) -> None:
    caplog.set_level(logging.INFO)
    run = run_fake_fun_ofdm(tmp_path, monkeypatch, HANGS)
    assert run.status == hardware.TIMED_OUT
    # Killed, since it ignores Ctrl+C
    assert run.returncode == -9
    assert run.output == "Hanging\n"
    assert "fun-ofdm: sleeping" in run.threads
    assert re.search(r"bpsk-1/2: \d+ s of 1 s", caplog.text)
    assert "bpsk-1/2: past its 1 s time limit" in caplog.text


def test_passes_on_one_ctrl_c_when_interrupted(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # fun-ofdm counts the Ctrl+Cs it gets, without stopping, so it's killed.
    # The run is interrupted once fun-ofdm is ready, as if by a Ctrl+C in
    # the terminal.
    monkeypatch.setattr(hardware, "KILL_AFTER_S", 0.5)
    count = tmp_path / "count"
    ready = tmp_path / "ready"
    fun_ofdm = tmp_path / "fun-ofdm"
    fun_ofdm.write_text(
        f"#!/bin/sh\ntrap 'echo INT >> {count}' INT\ntouch {ready}\n"
        "while :; do sleep 0.05; done\n"
    )
    fun_ofdm.chmod(0o755)

    def interrupted(*_: object) -> tuple[str, str, bool]:
        while not ready.exists():
            time.sleep(0.01)
        raise KeyboardInterrupt

    monkeypatch.setattr(hardware, "wait_for", interrupted)
    with pytest.raises(KeyboardInterrupt):
        hardware.run_fun_ofdm(
            hardware.Commands(), fun_ofdm, "bpsk-1/2", ["loopback"], 10
        )
    assert count.read_text() == "INT\n"


def test_a_run_is_in_a_session_of_its_own(tmp_path: pathlib.Path) -> None:
    # So a Ctrl+C in the terminal doesn't reach it as well.
    fun_ofdm = tmp_path / "fun-ofdm"
    fun_ofdm.write_text(
        "#!/bin/sh\nfor last; do :; done\n"
        'echo "{\\"sid\\": $(ps -o sid= -p $$)}" > "$last"\n'
    )
    fun_ofdm.chmod(0o755)
    run = hardware.run_fun_ofdm(hardware.Commands(), fun_ofdm, "x", [], 10)
    assert run.summary is not None
    assert run.summary["sid"] != os.getsid(0)
