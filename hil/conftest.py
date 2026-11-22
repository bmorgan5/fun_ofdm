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

import logging
import pathlib
from collections.abc import Callable, Iterator, Sequence
from typing import Any

import hardware
import pytest

#: Runs fun-ofdm: a name for the run, the arguments, and a time limit in
#: seconds.
FunOfdm = Callable[[str, Sequence[str], float], hardware.Run]

#: Says what the tests are doing as they do it, since a run takes minutes.
log = logging.getLogger("hil")

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


@pytest.fixture(scope="session")
def run() -> hardware.Commands:
    """Runs commands, as you until the usrp fixture finds it needs root."""
    return hardware.Commands()


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


@pytest.fixture(scope="session")
def usrp(run: hardware.Commands) -> hardware.Usrp:
    """The USRP, checked to be on USB 3 (hardware.find_usrp()).

    If UHD needs root to open it, every command from then on runs with
    `sudo -n`.
    """
    try:
        return hardware.find_usrp(run)
    except hardware.HardwareError as error:
        pytest.fail(str(error), pytrace=False)


@pytest.fixture(scope="session")
def fun_ofdm_json(fun_ofdm: pathlib.Path, run: hardware.Commands) -> FunOfdm:
    """Runs fun-ofdm with a time limit (hardware.run_fun_ofdm())."""

    def run_it(name: str, args: Sequence[str], limit_s: float) -> hardware.Run:
        return hardware.run_fun_ofdm(run, fun_ofdm, name, args, limit_s)

    return run_it


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
