"""Options and fixtures for the hardware tests.

A missing precondition, like no USRP, fails in a fixture, which pytest reports
as an error, while a missed exit criterion fails the test.

A run uses one topology, from --topology or, with --interactive, the person.
The tests are the same on every topology: they send frames over `link`, which
each topology provides. Each test file writes its results to a directory named after
it: test_link_smoke.py to link_smoke/.
"""

import contextlib
import dataclasses
import json
import pathlib
import subprocess
import tempfile
from collections.abc import Callable, Generator, Iterator, Sequence
from typing import Any

import hardware
import pytest

#: How long UHD's tools may take: uhd_usrp_probe loads the FPGA image.
UHD_TIMEOUT_S = 120

#: Runs fun-ofdm with arguments and a time limit in seconds, and returns its
#: --json summary.
FunOfdm = Callable[[Sequence[str], float], dict[str, Any]]

#: How long fun-ofdm gets to stop after a Ctrl+C at its time limit, before
#: it's killed.
KILL_AFTER_S = 10

#: Sends frames from the topology's transmitter to its receiver: at a rate, a
#: number of them. It returns fun-ofdm's --json summary, with what was sent and
#: received, and the check of the data.
Link = Callable[[str, int], dict[str, Any]]

#: A run's time limit: 30 s to open the USRPs, then 20 ms a frame, about twice
#: what a frame takes at BPSK 1/2.
RUN_START_S = 30
FRAME_S = 0.02

#: The seed of every run's data, so a run can be repeated.
SEED = 1


def pytest_addoption(parser: pytest.Parser) -> None:
    """Adds the hardware tests' options."""
    group = parser.getgroup("hil", "hardware tests (hil/README.md)")
    group.addoption(
        "--topology",
        choices=list(hardware.TOPOLOGIES),
        help="how the radios are connected (asked for with --interactive)",
    )
    group.addoption(
        "--interactive",
        action="store_true",
        help="walk through the topology, checking what can be checked, then "
        "explain the results",
    )
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
        help="where to write each test file's results (default: out/hil/)",
    )
    # On channel 36, a loopback cable's signal is about 31 dB stronger than
    # what leaks between the B210's channels inside it, against 19 dB on
    # channel 144 (5.72 GHz).
    group.addoption(
        "--freq",
        type=float,
        default=5.18,
        help="center frequency in GHz (default: %(default)s, channel 36)",
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
        "--atten-db",
        type=float,
        help="the attenuation in the cable, in dB, which the results record "
        "(asked for with --interactive)",
    )
    group.addoption(
        "--sudo",
        action="store_true",
        help="run UHD's tools and fun-ofdm with `sudo -n`, for access to the "
        "USB device and real time priority",
    )


class Guide:
    """Talks to the person running the tests, with --interactive.

    Without --interactive, it says nothing and asks nothing.
    """

    def __init__(self, config: pytest.Config) -> None:
        """Turns pytest's output capture off while it talks."""
        self.interactive: bool = config.getoption("--interactive")
        self._capture = config.pluginmanager.getplugin("capturemanager")

    @contextlib.contextmanager
    def _uncaptured(self) -> Generator[None]:
        # Stdin too (in_), which global_and_fixture_disabled() leaves captured
        if self._capture is None:
            yield
            return
        self._capture.suspend_global_capture(in_=True)
        try:
            yield
        finally:
            self._capture.resume_global_capture()

    def say(self, text: str) -> None:
        """Prints text, with --interactive."""
        if self.interactive:
            with self._uncaptured():
                print(f"\n{text}")

    def confirm(self, step: str) -> None:
        """Shows a step, and waits until it's done, with --interactive."""
        if self.interactive:
            with self._uncaptured():
                input(f"\n{step}\nPress Enter when it's done. ")

    def ask(self, question: str) -> str:
        """Asks a question, with --interactive, or returns ""."""
        if not self.interactive:
            return ""
        with self._uncaptured():
            return input(f"\n{question} ")


@dataclasses.dataclass(frozen=True)
class Runner:
    """Runs commands, with `sudo -n` if --sudo asks for it."""

    sudo: bool

    def __call__(
        self, command: Sequence[str], timeout: float | None = None
    ) -> subprocess.CompletedProcess[str]:
        """Runs a command, capturing its output, without checking its status."""
        prefix = ["sudo", "-n"] if self.sudo else []
        return subprocess.run(
            [*prefix, *command],
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout,
        )


@pytest.fixture(scope="session")
def guide(request: pytest.FixtureRequest) -> Guide:
    """Talks to the person running the tests, with --interactive."""
    return Guide(request.config)


@pytest.fixture(scope="session")
def run(request: pytest.FixtureRequest) -> Runner:
    """Runs commands, with `sudo -n` if --sudo asks for it."""
    return Runner(sudo=request.config.getoption("--sudo"))


@pytest.fixture(scope="session")
def fun_ofdm(request: pytest.FixtureRequest) -> pathlib.Path:
    """The fun-ofdm binary under test."""
    path: pathlib.Path = request.config.getoption("--bin")
    if not path.exists():
        pytest.fail(f"{path} doesn't exist: bazel run install")
    return path


@pytest.fixture(scope="session")
def atten_db(
    request: pytest.FixtureRequest, guide: Guide, topology: hardware.Topology
) -> float:
    """The attenuation, from --atten-db or, after the topology, the person."""
    value: float | None = request.config.getoption("--atten-db")
    while value is None and guide.interactive:
        try:
            value = float(guide.ask("How many dB of attenuation is there?"))
        except ValueError:
            continue
    if value is None:
        pytest.fail(
            "--atten-db is missing: the results mean nothing without the "
            "attenuation"
        )
    if value < hardware.MIN_ATTENUATION_DB:
        pytest.fail(
            f"{value:g} dB of attenuation is too little: use at least "
            f"{hardware.MIN_ATTENUATION_DB} dB, or the receiver can be damaged"
        )
    return value


@pytest.fixture(scope="session")
def radio(request: pytest.FixtureRequest, atten_db: float) -> dict[str, Any]:
    """The radio settings, which every result records."""
    return {
        "freq_ghz": request.config.getoption("--freq"),
        "tx_gain": request.config.getoption("--tx-gain"),
        "rx_gain": request.config.getoption("--rx-gain"),
        "atten_db": atten_db,
    }


def ask_for_topology(guide: Guide) -> str:
    """Shows every topology, and asks which one the person has."""
    names = list(hardware.TOPOLOGIES)
    lines = ["Which topology do you have?"]
    for number, (name, topology) in enumerate(hardware.TOPOLOGIES.items(), 1):
        support = "" if name in LINKS else " (not supported yet)"
        lines.append(
            f"\n{number}. {name}: {topology.description}{support}\n\n"
            f"{topology.diagram}"
        )
    guide.say("\n".join(lines))
    while True:
        try:
            answer = guide.ask("Its number or name:").strip().lower()
        except EOFError:
            pytest.fail("No topology chosen")
        if answer.isdigit() and 1 <= int(answer) <= len(names):
            answer = names[int(answer) - 1]
        if answer in LINKS:
            return answer


@pytest.fixture(scope="session")
def topology(request: pytest.FixtureRequest, guide: Guide) -> hardware.Topology:
    """The topology, from --topology or the person."""
    name: str | None = request.config.getoption("--topology")
    asked = name is None
    if name is None:
        if not guide.interactive:
            pytest.fail(
                f"Name the topology with --topology ({', '.join(LINKS)}), or "
                "choose it with --interactive"
            )
        name = ask_for_topology(guide)
    if name not in LINKS:
        pytest.fail(f"The {name} topology isn't supported yet")
    chosen = hardware.TOPOLOGIES[name]
    # The person who chose it has just seen its diagram
    diagram = "" if asked else f"\n\n{chosen.diagram}"
    guide.say(
        f"The {chosen.name} topology, {chosen.description}:{diagram}\n\n"
        f"{hardware.SAFETY}"
    )
    for number, step in enumerate(chosen.steps, 1):
        guide.confirm(f"{number}. {step}")
    return chosen


@pytest.fixture(scope="session")
def usrps(
    topology: hardware.Topology, run: Runner, guide: Guide
) -> list[hardware.Usrp]:
    """The topology's USRPs, each checked to be on USB 3."""
    found = run(["uhd_find_devices"], timeout=UHD_TIMEOUT_S)
    devices = hardware.parse_find_devices(found.stdout)
    if len(devices) < topology.usrps:
        on_usb = hardware.usrps_on_usb()
        if on_usb > len(devices):
            why = (
                f"{on_usb} are on USB, but UHD can't open them: try --sudo (in "
                "the dev container, USB devices are root's)"
            )
        else:
            why = (
                "Plug it in, and in the dev container, attach it with usbipd "
                "(README.md)"
            )
        pytest.fail(
            f"The {topology.name} topology needs {topology.usrps} USRP(s), and UHD "
            f"found {len(devices)}. {why}.\n" + found.stderr
        )
    chosen = devices[: topology.usrps]
    for usrp in chosen:
        probe = run(
            ["uhd_usrp_probe", "--args", f"serial={usrp.serial}"],
            timeout=UHD_TIMEOUT_S,
        )
        version = hardware.usb_version(probe.stdout + probe.stderr)
        if version != 3:
            pytest.fail(
                f"The {usrp.product} {usrp.serial} is on USB {version or '?'}"
                ", not USB 3: plug it into a USB 3 port, with a USB 3 cable"
            )
    guide.say(
        "Found "
        + ", ".join(f"{u.product} {u.serial}" for u in chosen)
        + ", on USB 3."
    )
    return chosen


@pytest.fixture(scope="session")
def fun_ofdm_json(fun_ofdm: pathlib.Path, run: Runner) -> Iterator[FunOfdm]:
    """Runs fun-ofdm with arguments and a time limit, and returns its summary.

    It fails the test if fun-ofdm runs past the limit, which means it hung,
    or doesn't write a --json summary, which means it crashed or rejected
    its arguments. At the limit, `timeout` sends a Ctrl+C, then kills it
    KILL_AFTER_S seconds later.
    """
    with tempfile.TemporaryDirectory() as tmp:

        def run_it(args: Sequence[str], limit_s: float) -> dict[str, Any]:
            summary = pathlib.Path(tmp) / "summary.json"
            summary.unlink(missing_ok=True)
            command = [str(fun_ofdm), *args, "--json", str(summary)]
            done = run(
                [
                    "timeout",
                    "--signal=INT",
                    f"--kill-after={KILL_AFTER_S}",
                    f"{limit_s:g}",
                    *command,
                ]
            )
            output = done.stdout + done.stderr
            if done.returncode in (124, 137):  # timeout stopped or killed it
                pytest.fail(
                    f"fun-ofdm {' '.join(args)} didn't finish within "
                    f"{limit_s:g} s, so it was stopped:\n{output}"
                )
            if not summary.exists() or not summary.stat().st_size:
                pytest.fail(
                    f"fun-ofdm {' '.join(args)} wrote no summary:\n{output}"
                )
            return json.loads(summary.read_text())

        yield run_it


def loopback_link(
    fun_ofdm_json: FunOfdm, usrps: list[hardware.Usrp], radio: dict[str, Any]
) -> Link:
    """The loopback topology's link: fun-ofdm loopback, on one B210."""

    def send(rate: str, packets: int) -> dict[str, Any]:
        return fun_ofdm_json(
            [
                "loopback",
                f"--device=serial={usrps[0].serial}",
                f"--freq={radio['freq_ghz']}",
                f"--tx-gain={radio['tx_gain']}",
                f"--rx-gain={radio['rx_gain']}",
                f"--phy-rate={rate}",
                f"--packets={packets}",
                f"--seed={SEED}",
            ],
            RUN_START_S + FRAME_S * packets,
        )

    return send


#: Makes each supported topology's link, by its name.
LINKS: dict[
    str,
    Callable[[FunOfdm, list[hardware.Usrp], dict[str, Any]], Link],
] = {"loopback": loopback_link}


@pytest.fixture(scope="session")
def link(
    topology: hardware.Topology,
    radio: dict[str, Any],
    usrps: list[hardware.Usrp],
    fun_ofdm_json: FunOfdm,
) -> Link:
    """Sends frames from the topology's transmitter to its receiver.

    It fails the test if the USRPs sent no samples.
    """
    send = LINKS[topology.name](fun_ofdm_json, usrps, radio)

    def checked(rate: str, packets: int) -> dict[str, Any]:
        summary = send(rate, packets)
        if not hardware.streaming(summary):
            pytest.fail(
                "The USRP sent no samples: it isn't streaming. Power-cycle it."
            )
        return summary

    return checked


class Results:
    """A test file's results, which it writes when the file's tests end."""

    def __init__(self) -> None:
        """Starts with no results."""
        self.runs: dict[str, Any] = {}
        self.rows: list[dict[str, str]] = []

    def add(self, name: str, raw: Any, row: dict[str, str]) -> None:
        """Adds a result: its raw data, and its row of result.md."""
        self.runs[name] = raw
        self.rows.append(row)


@pytest.fixture(scope="module")
def results(
    request: pytest.FixtureRequest,
    fun_ofdm: pathlib.Path,
    topology: hardware.Topology,
    usrps: list[hardware.Usrp],
    radio: dict[str, Any],
    guide: Guide,
) -> Iterator[Results]:
    """The test file's results, written to result.json and result.md."""
    collected = Results()
    yield collected
    if not collected.runs:
        return

    name = request.path.stem.removeprefix("test_")
    out: pathlib.Path = (
        request.config.getoption("--out") or hardware.REPO / "out" / "hil"
    )
    title = (request.module.__doc__ or name).split("\n")[0].rstrip(".")
    hardware.write_results(
        out / name,
        title,
        hardware.metadata(
            fun_ofdm, usrps, {"topology": topology.name, **radio}
        ),
        collected.runs,
        collected.rows,
    )
    guide.say(
        (out / name / "result.md").read_text()
        + f"\nWritten to {out / name}/result.json and result.md."
    )
