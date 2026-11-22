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
        str(REPO / "tools" / "results"),
        str(REPO / "tools" / "scorecard"),
    ]
    monkeypatch.setenv("PYTHONPATH", os.pathsep.join(paths))
    return pytester


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


# A test of the usrp fixture, with a USRP on USB that UHD can't open, and a
# sudo that needs a password.
USRP_TEST = """
import hardware

hardware.usrps_on_usb = lambda: 1

def test_usrp(usrp):
    pass
"""


def test_reports_a_usrp_that_isnt_ready_as_an_error(
    hil_pytester: pytest.Pytester, monkeypatch: pytest.MonkeyPatch
) -> None:
    bin_dir = hil_pytester.path / "bin"
    bin_dir.mkdir()
    for name in ("uhd_find_devices", "sudo"):
        (bin_dir / name).write_text("#!/bin/sh\nexit 1\n")
        (bin_dir / name).chmod(0o755)
    monkeypatch.setenv("PATH", f"{bin_dir}{os.pathsep}{os.environ['PATH']}")
    hil_pytester.makepyfile(USRP_TEST)
    result = hil_pytester.run(sys.executable, "-m", "pytest")
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*sudo needs a password*"])


# A test that the usrp fixture's sudo reaches fun_ofdm_json, which runs
# fun-ofdm, with a USRP that UHD opens only as root.
SUDO_TEST = """
import hardware

hardware.usrps_on_usb = lambda: 1

def test_sudo(usrp, fun_ofdm_json):
    print("summary:", fun_ofdm_json("x", [], 10).summary)
"""

# Writes whether it ran as the fake root to its summary, the last argument.
FUN_OFDM = """\
#!/bin/sh
for last; do :; done
echo "{\\"root\\": \\"$FAKE_ROOT\\"}" > "$last"
"""

FIND_AS_ROOT = """\
#!/bin/sh
[ "$FAKE_ROOT" = 1 ] && printf -- '-- UHD Device 0\\n    product: B210\\n'
"""

SUDO = """\
#!/bin/sh
shift  # -n
[ "$1" = true ] && exit 0
FAKE_ROOT=1 exec "$@"
"""


def test_runs_fun_ofdm_with_sudo_once_the_usrp_needs_it(
    hil_pytester: pytest.Pytester, monkeypatch: pytest.MonkeyPatch
) -> None:
    bin_dir = hil_pytester.path / "bin"
    bin_dir.mkdir()
    for name, script in (
        ("uhd_find_devices", FIND_AS_ROOT),
        ("uhd_usrp_probe", "#!/bin/sh\necho 'Operating over USB 3.'\n"),
        ("sudo", SUDO),
        ("fun-ofdm", FUN_OFDM),
    ):
        (bin_dir / name).write_text(script)
        (bin_dir / name).chmod(0o755)
    monkeypatch.setenv("PATH", f"{bin_dir}{os.pathsep}{os.environ['PATH']}")
    hil_pytester.makepyfile(SUDO_TEST)
    result = hil_pytester.run(
        sys.executable, "-m", "pytest", "-s", f"--bin={bin_dir / 'fun-ofdm'}"
    )
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*summary: {'root': '1'}"])


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
