"""Options and fixtures for the hardware tests.

A missing precondition, like no USRP, fails in a fixture, which pytest reports
as an error, while a missed exit criterion fails the test.

The tests send frames over `link`: one B210, cabled from one of its ports to
another through an attenuator (hil/README.md).

UHD's tools and fun-ofdm run as you, or with `sudo -n` if UHD can't open the
USRP without it, like in the dev container, where USB devices are root's.

Sending frames never fails a test by itself: each run of fun-ofdm is recorded
however it ends, and the tests check what it did. pytest prints each test
file's results at the end, whether the tests passed or not. With --out DIR,
each test file also writes them, with each run's log, to a directory in DIR
named after it (test_link_smoke.py to link_smoke/).
"""

import contextlib
import dataclasses
import json
import logging
import os
import pathlib
import signal
import subprocess
import tempfile
import time
from collections.abc import Callable, Iterator, Sequence
from typing import Any

import hardware
import pytest

#: How long UHD's tools may take: uhd_usrp_probe loads the FPGA image.
UHD_TIMEOUT_S = 120

#: Runs fun-ofdm: a name for the run, the arguments, and a time limit in
#: seconds.
FunOfdm = Callable[[str, Sequence[str], float], hardware.Run]

#: Says what the tests are doing as they do it, since a run takes minutes.
log = logging.getLogger("hil")

#: How often a run says how long it has been running, in seconds.
HEARTBEAT_S = 15

#: How long fun-ofdm gets to stop after a Ctrl+C at its time limit, before
#: it's killed.
KILL_AFTER_S = 10

#: Sends frames from the transmitter to the receiver: a name for
#: the run, the rate, and how many frames. The run's summary has what was sent
#: and received, and the check of the data.
Link = Callable[[str, str, int], hardware.Run]

#: Each test file's results, as Markdown, and where they were written, if
#: they were: the session prints them at the end.
RESULTS = pytest.StashKey[list[tuple[str, pathlib.Path | None]]]()

#: A run's time limit: 30 s to open the USRP, then 20 ms a frame at 5 MHz,
#: about twice what a frame takes at BPSK 1/2, and in proportion at other
#: bandwidths.
RUN_START_S = 30
FRAME_S = 0.02
FRAME_S_BANDWIDTH_MHZ = 5

#: The 802.11 channel the runs use without --channel or --freq: 36, at
#: 5.18 GHz. There, a cable's signal is about 31 dB stronger than what leaks
#: between the B210's channels inside it, against 19 dB on channel 144
#: (5.72 GHz).
DEFAULT_CHANNEL = 36

#: The seed of every run's data, so a run can be repeated.
SEED = 1


def pytest_configure(config: pytest.Config) -> None:
    """Formats pytest's live log, which --log-cli-level=INFO turns on.

    It shows each run's progress as it happens. pytest's logging plugin reads
    these after this hook, so they act like its format options.
    """
    option = config.option
    if option.log_cli_format is None:
        option.log_cli_format = "%(asctime)s %(message)s"
    if option.log_cli_date_format is None:
        option.log_cli_date_format = "%H:%M:%S"


def pytest_addoption(parser: pytest.Parser) -> None:
    """Adds the hardware tests' options."""
    group = parser.getgroup("hil", "hardware tests (hil/README.md)")
    group.addoption(
        "--bin",
        type=pathlib.Path,
        default=hardware.DEFAULT_FUN_OFDM,
        help="the fun-ofdm binary to test (default: the one `bazel run "
        "install` copies)",
    )
    group.addoption(
        "--out",
        type=pathlib.Path,
        help="also write each test file's results and run logs to "
        "DIR/<test>/ (default: only print the results)",
    )
    group.addoption(
        "--channel",
        type=int,
        help=f"802.11 channel number (default: {DEFAULT_CHANNEL})",
    )
    group.addoption(
        "--freq",
        type=float,
        help="center frequency in GHz, instead of --channel",
    )
    group.addoption(
        "--bandwidth",
        type=float,
        default=5,
        help="channel bandwidth in MHz, also the sample rate (default: "
        "%(default)s)",
    )
    group.addoption(
        "--tx-gain",
        type=float,
        default=80,
        help="transmit gain in dB (default: %(default)s)",
    )
    group.addoption(
        "--rx-gain",
        type=float,
        default=60,
        help="receive gain in dB (default: %(default)s)",
    )
    group.addoption(
        "--tx-port",
        choices=list(hardware.PORTS),
        default=hardware.DEFAULT_TX_PORT,
        help="the port the transmitter is cabled to (default: %(default)s)",
    )
    group.addoption(
        "--rx-port",
        choices=list(hardware.PORTS),
        default=hardware.DEFAULT_RX_PORT,
        help="the port the receiver is cabled to (default: %(default)s)",
    )


@dataclasses.dataclass
class Runner:
    """Runs commands, as root with `sudo -n` once the USRP needs it."""

    #: Whether commands run with `sudo -n`, which the usrp fixture decides.
    sudo: bool = False

    def __call__(
        self, command: Sequence[str], timeout: float | None = None
    ) -> subprocess.CompletedProcess[str]:
        """Runs a command, capturing its output, without checking its status."""
        return subprocess.run(
            [*self.prefix, *command],
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout,
        )

    @property
    def prefix(self) -> list[str]:
        """What goes before each command: `sudo -n`, or nothing."""
        return ["sudo", "-n"] if self.sudo else []

    def stdout(self, command: Sequence[str]) -> str | None:
        """Runs a command, and returns its stdout, or None if it fails.

        It's a hardware.Runner.
        """
        done = self(command, timeout=UHD_TIMEOUT_S)
        return done.stdout if done.returncode == 0 else None

    def send_signal(self, pid: int, sig: signal.Signals) -> None:
        """Sends a signal to a process, which is root's with sudo."""
        if self.sudo:
            self(["kill", f"-{sig.name.removeprefix('SIG')}", str(pid)])
        else:
            with contextlib.suppress(ProcessLookupError):
                os.kill(pid, sig)


@pytest.fixture(scope="session")
def run() -> Runner:
    """Runs commands, as you until the usrp fixture finds it needs root."""
    return Runner()


@pytest.fixture(scope="session")
def fun_ofdm(request: pytest.FixtureRequest) -> pathlib.Path:
    """The fun-ofdm binary under test."""
    path: pathlib.Path = request.config.getoption("--bin")
    if not path.exists():
        pytest.fail(f"{path} doesn't exist: bazel run install")
    return path


@pytest.fixture(scope="session")
def radio(
    request: pytest.FixtureRequest, ports: tuple[str, str]
) -> dict[str, Any]:
    """The radio settings, which every result records.

    It has the channel or the frequency, whichever was given.
    """
    channel: int | None = request.config.getoption("--channel")
    freq_ghz: float | None = request.config.getoption("--freq")
    if channel is not None and freq_ghz is not None:
        pytest.fail("Give --channel or --freq, not both", pytrace=False)
    tuning = (
        {"freq_ghz": freq_ghz}
        if freq_ghz is not None
        else {"channel": channel or DEFAULT_CHANNEL}
    )
    return {
        **tuning,
        "bandwidth_mhz": request.config.getoption("--bandwidth"),
        "tx_gain": request.config.getoption("--tx-gain"),
        "rx_gain": request.config.getoption("--rx-gain"),
        "tx_port": ports[0],
        "rx_port": ports[1],
    }


@pytest.fixture(scope="session")
def ports(request: pytest.FixtureRequest) -> tuple[str, str]:
    """The transmitter's and receiver's ports, as names in hardware.PORTS."""
    tx: str = request.config.getoption("--tx-port")
    rx: str = request.config.getoption("--rx-port")
    problem = hardware.port_problem(hardware.PORTS[tx], hardware.PORTS[rx])
    if problem:
        pytest.fail(problem)
    return tx, rx


def find_usrps(run: Runner) -> tuple[list[hardware.Usrp], str]:
    """The USRPs that uhd_find_devices finds, and what it wrote to stderr."""
    found = run(["uhd_find_devices"], timeout=UHD_TIMEOUT_S)
    return hardware.parse_find_devices(found.stdout), found.stderr


@pytest.fixture(scope="session")
def usrp(run: Runner) -> hardware.Usrp:
    """The USRP, checked to be on USB 3.

    If UHD can't open a USRP that's on USB, it tries again as root, and from
    then on runs every command with `sudo -n`.
    """
    devices, errors = find_usrps(run)
    on_usb = hardware.usrps_on_usb()
    if not devices and on_usb:
        if run(["sudo", "-n", "true"]).returncode != 0:
            pytest.fail(
                f"{on_usb} USRP(s) are on USB, but UHD can't open them as you, "
                "and sudo needs a password: run `sudo -v` first, or install "
                "UHD's udev rules (hil/README.md)",
                pytrace=False,
            )
        log.info("UHD can't open the USRP as you, so running it with sudo")
        run.sudo = True
        devices, errors = find_usrps(run)
    if not devices:
        pytest.fail(
            "UHD found no USRP: plug one in, and in the dev container, attach "
            "it with usbipd (README.md).\n" + errors,
            pytrace=False,
        )
    usrp = devices[0]
    log.info("Probing the %s %s", usrp.product, usrp.serial)
    probe = run(
        ["uhd_usrp_probe", "--args", f"serial={usrp.serial}"],
        timeout=UHD_TIMEOUT_S,
    )
    version = hardware.usb_version(probe.stdout + probe.stderr)
    if version != 3:
        pytest.fail(
            f"The {usrp.product} {usrp.serial} is on USB {version or '?'}, "
            "not USB 3: plug it into a USB 3 port, with a USB 3 cable",
            pytrace=False,
        )
    return usrp


def stop(
    process: subprocess.Popen[str], name: str, run: Runner
) -> tuple[str, str]:
    """Stops a run that's past its time limit, and says where it was stuck.

    It reads where each of fun-ofdm's threads is waiting, sends it a Ctrl+C,
    and kills it if it hasn't stopped KILL_AFTER_S seconds later.

    Args:
        process: The run, which can be `sudo` running fun-ofdm.
        name: fun-ofdm's file name.
        run: Runs commands, with sudo if fun-ofdm runs as root.

    Returns:
        Where each thread was waiting, and the run's output.
    """
    pid = hardware.find_process(process.pid, name, run.stdout)
    if pid is None:
        pid = process.pid
        threads = f"No process named {name} under {pid}.\n"
    else:
        threads = hardware.thread_states(pid, run.stdout)
    run.send_signal(pid, signal.SIGINT)
    try:
        output, _ = process.communicate(timeout=KILL_AFTER_S)
    except subprocess.TimeoutExpired:
        run.send_signal(pid, signal.SIGKILL)
        output, _ = process.communicate()
    return threads, output


@pytest.fixture(scope="session")
def fun_ofdm_json(fun_ofdm: pathlib.Path, run: Runner) -> Iterator[FunOfdm]:
    """Runs fun-ofdm with a time limit, and records how the run ended.

    A run that passes its limit is stopped (see stop()), and a run that
    writes no --json summary crashed or rejected its arguments. Either way,
    the test that asked for the run decides whether that fails it.
    """
    with tempfile.TemporaryDirectory() as tmp:

        def run_it(
            name: str, args: Sequence[str], limit_s: float
        ) -> hardware.Run:
            summary = pathlib.Path(tmp) / "summary.json"
            summary.unlink(missing_ok=True)
            start = time.monotonic()
            process = subprocess.Popen(
                [*run.prefix, str(fun_ofdm), *args, "--json", str(summary)],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            threads = ""
            deadline = start + limit_s
            while True:
                wait_s = min(HEARTBEAT_S, deadline - time.monotonic())
                try:
                    # Calling it again after a timeout loses no output
                    output, _ = process.communicate(timeout=max(wait_s, 0))
                    timed_out = False
                    break
                except subprocess.TimeoutExpired:
                    elapsed_s = time.monotonic() - start
                    if elapsed_s >= limit_s:
                        log.info(
                            "%s: past its %g s time limit, so reading its "
                            "threads and stopping it",
                            name,
                            limit_s,
                        )
                        threads, output = stop(process, fun_ofdm.name, run)
                        timed_out = True
                        break
                    log.info("%s: %.0f s of %g s", name, elapsed_s, limit_s)
            seconds = time.monotonic() - start
            data = None
            with contextlib.suppress(OSError, ValueError):
                data = json.loads(summary.read_text())
            if timed_out:
                status = hardware.TIMED_OUT
            elif data is None:
                status = hardware.CRASHED
            else:
                status = hardware.FINISHED
            ended = status
            if status == hardware.CRASHED:
                ended += f" with status {process.returncode}"
            log.info("%s: %s after %.0f s", name, ended, seconds)
            return hardware.Run(
                name=name,
                status=status,
                seconds=seconds,
                limit_s=limit_s,
                returncode=process.returncode,
                output=output,
                summary=data,
                threads=threads,
            )

        yield run_it


def tuning_args(radio: dict[str, Any]) -> list[str]:
    """fun-ofdm's options for the radio's channel or frequency."""
    if "freq_ghz" in radio:
        return [f"--freq={radio['freq_ghz']}"]
    return [f"--channel={radio['channel']}"]


@pytest.fixture(scope="session")
def link(
    radio: dict[str, Any], usrp: hardware.Usrp, fun_ofdm_json: FunOfdm
) -> Link:
    """Sends frames from the transmitter to the receiver: fun-ofdm loopback."""
    frame_s = FRAME_S * FRAME_S_BANDWIDTH_MHZ / radio["bandwidth_mhz"]

    def send(name: str, rate: str, packets: int) -> hardware.Run:
        log.info("%s: sending %d frames at %s", name, packets, rate)
        return fun_ofdm_json(
            name,
            [
                "loopback",
                f"--device=serial={usrp.serial}",
                *tuning_args(radio),
                f"--bandwidth={radio['bandwidth_mhz']}",
                f"--tx-gain={radio['tx_gain']}",
                f"--rx-gain={radio['rx_gain']}",
                *hardware.port_args(
                    hardware.PORTS[radio["tx_port"]],
                    hardware.PORTS[radio["rx_port"]],
                ),
                f"--phy-rate={rate}",
                f"--packets={packets}",
                f"--seed={SEED}",
            ],
            RUN_START_S + frame_s * packets,
        )

    return send


class Results:
    """A test file's results.

    With somewhere to write them, it writes each run's log as the run ends,
    then result.json and result.md when the file's tests end.
    """

    def __init__(self, out: pathlib.Path | None) -> None:
        """Starts with no results, to write to out, if it's given."""
        self.out = out
        self.runs: dict[str, Any] = {}
        self.rows: list[dict[str, str]] = []

    def add(self, run: hardware.Run) -> None:
        """Writes a run's log, if the results are written, and adds the run."""
        log = self.log(run)
        if log:
            log.parent.mkdir(parents=True, exist_ok=True)
            log.write_text(hardware.run_log(run))
        self.runs[run.name] = {
            "status": run.status,
            "seconds": run.seconds,
            "limit_s": run.limit_s,
            "returncode": run.returncode,
            "log": run.log_name,
            "summary": run.summary,
        }
        self.rows.append(hardware.run_row(run))

    def log(self, run: hardware.Run) -> pathlib.Path | None:
        """A run's log file, or None if the results aren't written."""
        return self.out / run.log_name if self.out else None

    def explain(self, run: hardware.Run) -> str:
        """Why a run's test failed, for its message."""
        return hardware.explain(run, self.log(run))


def report(
    config: pytest.Config, markdown: str, written: pathlib.Path | None
) -> None:
    """Prints a test file's results at the end of the session.

    Args:
        config: The session's config.
        markdown: The results, as result.md has them.
        written: The directory they were written to, or None.
    """
    config.stash.setdefault(RESULTS, []).append((markdown, written))


def pytest_terminal_summary(
    terminalreporter: pytest.TerminalReporter, config: pytest.Config
) -> None:
    """Prints each test file's results, pass or fail."""
    for markdown, written in config.stash.get(RESULTS, []):
        where = f", written to {written}" if written else ""
        terminalreporter.write_sep("=", f"Results{where}")
        terminalreporter.write(markdown)


@pytest.fixture(scope="module")
def results(
    request: pytest.FixtureRequest,
    fun_ofdm: pathlib.Path,
    usrp: hardware.Usrp,
    radio: dict[str, Any],
) -> Iterator[Results]:
    """The test file's results: printed, and with --out, written."""
    name = request.path.stem.removeprefix("test_")
    out: pathlib.Path | None = request.config.getoption("--out")
    collected = Results(out / name if out else None)
    yield collected
    if not collected.runs:
        return

    title = (request.module.__doc__ or name).split("\n")[0].rstrip(".")
    meta = hardware.metadata(fun_ofdm, [usrp], radio)
    if collected.out:
        hardware.write_results(
            collected.out, title, meta, collected.runs, collected.rows
        )
    report(
        request.config,
        hardware.results_markdown(title, meta, collected.rows),
        collected.out,
    )
