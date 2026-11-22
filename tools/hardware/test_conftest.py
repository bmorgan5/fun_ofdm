import os
import pathlib
import sys

import pytest

pytest_plugins = ["pytester"]

REPO = pathlib.Path(__file__).resolve().parents[2]


@pytest.fixture
def hil_pytester(
    pytester: pytest.Pytester, monkeypatch: pytest.MonkeyPatch
) -> pytest.Pytester:
    # hil/conftest.py, in a directory of its own, able to import its tools.
    pytester.makeconftest((REPO / "hil" / "conftest.py").read_text())
    paths = [
        str(REPO / "tools" / "hardware"),
        str(REPO / "tools" / "scorecard"),
    ]
    monkeypatch.setenv("PYTHONPATH", os.pathsep.join(paths))
    return pytester


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

# A test of one run of the fake fun-ofdm, with a 1 s limit.
RUN_TEST = """
import conftest

conftest.KILL_AFTER_S = 0.5
conftest.HEARTBEAT_S = 0.3

def test_run(fun_ofdm_json):
    run = fun_ofdm_json("bpsk-1/2", ["loopback"], 1)
    print("status:", run.status)
    print("returncode:", run.returncode)
    print("summary:", run.summary)
    print("output:", run.output.strip())
    print("threads:", run.threads.strip() or "none")
"""


def run_fake_fun_ofdm(
    hil_pytester: pytest.Pytester,
    script: str,
    args: tuple[str, ...] = ("--log-cli-level=INFO",),
) -> pytest.RunResult:
    fun_ofdm = hil_pytester.path / "fun-ofdm"
    fun_ofdm.write_text(script)
    fun_ofdm.chmod(0o755)
    hil_pytester.makepyfile(RUN_TEST)
    return hil_pytester.run(
        sys.executable,
        "-m",
        "pytest",
        "-s",
        f"--bin={fun_ofdm}",
        *args,
    )


def test_records_a_run_that_finishes(hil_pytester: pytest.Pytester) -> None:
    # It finished even though it exited with 1, which is a failed check
    result = run_fake_fun_ofdm(hil_pytester, FINISHES)
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(
        [
            # The log, which pytest prints before what the test prints
            "*bpsk-1/2: finished after * s",
            "status: finished",
            "returncode: 1",
            "summary: {'sent': {'packets': 3}}",
            "output: Looping back",
            "threads: none",
        ]
    )


def test_records_a_run_that_crashes(hil_pytester: pytest.Pytester) -> None:
    result = run_fake_fun_ofdm(hil_pytester, CRASHES)
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(
        [
            "*bpsk-1/2: crashed with status 3 after * s",
            "status: crashed",
            "returncode: 3",
            "summary: None",
            "output: Error: no USRP",
        ]
    )


def test_stops_a_run_that_hangs_and_says_where(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_fake_fun_ofdm(hil_pytester, HANGS)
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(
        [
            "*bpsk-1/2: 0 s of 1 s",
            "*bpsk-1/2: past its 1 s time limit, so reading its threads*",
            "*bpsk-1/2: timed out after * s",
            "status: timed out",
            # Killed, since it ignores Ctrl+C
            "returncode: -9",
            "output: Hanging",
            "threads: * fun-ofdm: sleeping*",
        ]
    )


# A test that needs the ports.
PORTS_TEST = """
def test_ports(ports):
    print("Ports:", *ports)
"""


def run_ports_test(
    hil_pytester: pytest.Pytester, *args: str
) -> pytest.RunResult:
    hil_pytester.makepyfile(PORTS_TEST)
    return hil_pytester.run(sys.executable, "-m", "pytest", "-s", *args)


def test_ports_default_to_b_tx_rx_then_b_rx2(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_ports_test(hil_pytester)
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*Ports: B:TX/RX B:RX2"])


def test_ports_are_the_ones_named(hil_pytester: pytest.Pytester) -> None:
    result = run_ports_test(
        hil_pytester, "--tx-port=A:TX/RX", "--rx-port=B:TX/RX"
    )
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*Ports: A:TX/RX B:TX/RX"])


def test_an_rx2_port_cant_transmit(hil_pytester: pytest.Pytester) -> None:
    result = run_ports_test(hil_pytester, "--tx-port=A:RX2")
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*RF A RX2 can't transmit*"])


def test_one_port_cant_send_and_receive(hil_pytester: pytest.Pytester) -> None:
    result = run_ports_test(
        hil_pytester, "--tx-port=B:TX/RX", "--rx-port=B:TX/RX"
    )
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*RF B TX/RX can't be both*"])


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
echo "Operating over USB 3."
"""

# A test of the usrp fixture, with one Ettus device on USB.
USRPS_TEST = """
import hardware

hardware.usrps_on_usb = lambda: 1

def test_usrp(usrp, run):
    print("USRP:", usrp.serial, "sudo:", run.sudo)
"""


def run_usrps_test(
    hil_pytester: pytest.Pytester,
    monkeypatch: pytest.MonkeyPatch,
    sudo_exit: int,
    root: bool = False,
) -> pytest.RunResult:
    bin_dir = hil_pytester.path / "bin"
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
    hil_pytester.makepyfile(USRPS_TEST)
    return hil_pytester.run(sys.executable, "-m", "pytest", "-s")


def test_runs_as_you_when_uhd_can_open_the_usrp(
    hil_pytester: pytest.Pytester, monkeypatch: pytest.MonkeyPatch
) -> None:
    result = run_usrps_test(hil_pytester, monkeypatch, sudo_exit=1, root=True)
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*USRP: F571FA sudo: False"])


def test_uses_sudo_when_uhd_cant_open_the_usrp_as_you(
    hil_pytester: pytest.Pytester, monkeypatch: pytest.MonkeyPatch
) -> None:
    result = run_usrps_test(hil_pytester, monkeypatch, sudo_exit=0)
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*USRP: F571FA sudo: True"])


def test_says_to_run_sudo_v_when_sudo_needs_a_password(
    hil_pytester: pytest.Pytester, monkeypatch: pytest.MonkeyPatch
) -> None:
    result = run_usrps_test(hil_pytester, monkeypatch, sudo_exit=1)
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(
        ["*sudo needs a password: run `sudo -v` first, or install UHD's udev*"]
    )


# A test of the radio settings.
RADIO_TEST = """
def test_radio(radio):
    print("Radio:", radio)
"""


def run_radio_test(
    hil_pytester: pytest.Pytester, *args: str
) -> pytest.RunResult:
    hil_pytester.makepyfile(RADIO_TEST)
    return hil_pytester.run(sys.executable, "-m", "pytest", "-s", *args)


def test_tunes_to_channel_36_by_default(hil_pytester: pytest.Pytester) -> None:
    result = run_radio_test(hil_pytester)
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(
        ["*Radio: {'channel': 36, 'bandwidth_mhz': 5, *"]
    )


def test_tunes_to_a_frequency_or_a_channel(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_radio_test(hil_pytester, "--freq=5.2", "--bandwidth=20")
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(
        ["*Radio: {'freq_ghz': 5.2, 'bandwidth_mhz': 20.0, *"]
    )


def test_refuses_a_channel_and_a_frequency(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_radio_test(hil_pytester, "--channel=40", "--freq=5.2")
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*Give --channel or --freq, not both*"])
