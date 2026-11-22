"""What the hardware tests in hil/ share: the ports, UHD's output, results.

It's plain functions, so its tests run in CI, without hardware.
"""

import dataclasses
import json
import pathlib
import platform
import re
from collections.abc import Callable, Sequence
from typing import Any

import hostcheck

REPO = pathlib.Path(__file__).resolve().parents[2]

#: The fun-ofdm binary that `bazel run install` copies, for this machine.
DEFAULT_FUN_OFDM = (
    REPO / "out" / f"linux_{platform.machine()}" / "bin" / "fun-ofdm"
)

#: Runs a command and returns its stdout, or None if it can't be run or
#: fails.
Runner = Callable[[Sequence[str]], str | None]


@dataclasses.dataclass(frozen=True)
class Port:
    """One of a B210's RF ports: a channel, A or B, and its TX/RX or RX2 port."""

    channel: str
    antenna: str

    def __str__(self) -> str:
        """How the B210's case labels it, like "RF A TX/RX"."""
        return f"RF {self.channel} {self.antenna}"

    @property
    def subdev(self) -> str:
        """Its channel as a UHD subdevice spec, like "A:A"."""
        return f"A:{self.channel}"

    @property
    def can_transmit(self) -> bool:
        """Whether it can transmit: only the TX/RX ports can."""
        return self.antenna == "TX/RX"


#: A B210's ports, by their names for --tx-port and --rx-port.
PORTS = {
    f"{channel}:{antenna}": Port(channel, antenna)
    for channel in ("A", "B")
    for antenna in ("TX/RX", "RX2")
}

#: The ports the hardware tests use unless told otherwise.
DEFAULT_TX_PORT = "B:TX/RX"
DEFAULT_RX_PORT = "B:RX2"


def port_problem(tx: Port, rx: Port) -> str | None:
    """Why a B210 can't send from tx to rx, or None if it can."""
    if not tx.can_transmit:
        return f"{tx} can't transmit: only the TX/RX ports can"
    if tx == rx:
        return f"{tx} can't be both the transmitter and the receiver"
    return None


def port_args(tx: Port, rx: Port) -> list[str]:
    """fun-ofdm's options to send from tx and receive on rx."""
    return [
        f"--tx-subdev={tx.subdev}",
        f"--tx-antenna={tx.antenna}",
        f"--rx-subdev={rx.subdev}",
        f"--rx-antenna={rx.antenna}",
    ]


# TODO: tests with two USRPs: cabled to each other, and over the air with
# antennas, which is the same with antennas in place of the cable.


@dataclasses.dataclass(frozen=True)
class Usrp:
    """A USRP that uhd_find_devices found."""

    serial: str
    #: The model, like "B210".
    product: str


def parse_find_devices(output: str) -> list[Usrp]:
    """The USRPs in uhd_find_devices' output, in its order."""
    usrps = []
    for block in re.split(r"-- UHD Device \d+", output)[1:]:
        fields = dict(
            re.findall(r"^\s*(\w+):[ \t]*(.*?)\s*$", block, re.MULTILINE)
        )
        usrps.append(
            Usrp(serial=fields.get("serial", ""), product=fields["product"])
        )
    return usrps


#: Ettus Research's USB vendor ID, which every B2xx has.
ETTUS_USB_VENDOR = "2500"


def usrps_on_usb(sysfs: pathlib.Path = pathlib.Path("/sys")) -> int:
    """How many Ettus devices are on USB, whether or not UHD can open them."""
    return sum(
        (device / "idVendor").read_text().strip() == ETTUS_USB_VENDOR
        for device in (sysfs / "bus" / "usb" / "devices").glob("*")
        if (device / "idVendor").exists()
    )


def usb_version(probe_output: str) -> int | None:
    """The USB version a B2xx runs over, from uhd_usrp_probe's output."""
    match = re.search(r"Operating over USB (\d)", probe_output)
    return int(match.group(1)) if match else None


def uhd_version(output: str | None) -> str | None:
    """UHD's version, from `uhd_config_info --version`."""
    match = re.search(r"UHD (\S+)", output or "")
    return match.group(1) if match else None


def metadata(
    fun_ofdm: pathlib.Path,
    usrps: Sequence[Usrp],
    settings: dict[str, Any],
    run: Runner = hostcheck.run_command,
) -> dict[str, Any]:
    """What every result.json records besides the results.

    Args:
        fun_ofdm: The binary under test.
        usrps: The USRPs the test used.
        settings: The radio settings: frequency, gains, ports.
        run: Runs uhd_config_info, and hostcheck's commands.

    Returns:
        The host (hostcheck's report, with the kernel and commit), the UHD
        version, the USRPs and the settings.
    """
    return {
        "host": hostcheck.to_json(
            hostcheck.collect(fun_ofdm=fun_ofdm, run=run)
        ),
        "uhd_version": uhd_version(run(["uhd_config_info", "--version"])),
        "usrps": [dataclasses.asdict(usrp) for usrp in usrps],
        "settings": settings,
    }


#: How a run of fun-ofdm ended: it exited and wrote its summary...
FINISHED = "finished"
#: ...it ran past its time limit, and was stopped...
TIMED_OUT = "timed out"
#: ...or it exited without writing a summary.
CRASHED = "crashed"


@dataclasses.dataclass(frozen=True)
class Run:
    """One run of fun-ofdm, however it ended."""

    #: What the test calls it, like "bpsk-1/2".
    name: str
    #: FINISHED, TIMED_OUT or CRASHED.
    status: str
    #: How long it ran.
    seconds: float
    #: Its time limit.
    limit_s: float
    #: Its exit status, which is fun-ofdm's own unless it was killed.
    returncode: int | None
    #: Its stdout and stderr, interleaved. Like the summary and threads,
    #: it's left out of the repr, which pytest prints in a failure.
    output: str = dataclasses.field(repr=False)
    #: Its --json summary, or None if it wrote none. A run stopped at its
    #: time limit can still write one, without the check of the data.
    summary: dict[str, Any] | None = dataclasses.field(repr=False)
    #: Where each of its threads was waiting when it ran past its time
    #: limit, from thread_states().
    threads: str = dataclasses.field(default="", repr=False)

    @property
    def log_name(self) -> str:
        """The name of its log file, like "bpsk-1-2.log"."""
        return self.name.replace("/", "-") + ".log"


#: How many of a run's last lines a failure shows.
TAIL_LINES = 20


def run_log(run: Run) -> str:
    """A run's log file: its output, then where its threads were waiting."""
    log = run.output
    if run.threads:
        log += (
            f"\n--- Threads, at the {run.limit_s:g} s time limit ---\n"
            + run.threads
        )
    return log


def explain(run: Run, log: pathlib.Path | None) -> str:
    """Why a run's test failed: how it ended, then the end of its output.

    Args:
        run: The run.
        log: Where its whole log was written, or None if it wasn't (--out).
    """
    if run.status == TIMED_OUT:
        how = (
            f"{run.name} didn't finish within {run.limit_s:g} s, so it was "
            "stopped. Where each thread was waiting is at the end of its log"
        )
    elif run.status == CRASHED:
        how = (
            f"{run.name} exited with status {run.returncode} after "
            f"{run.seconds:.0f} s, without writing a summary"
        )
    else:
        how = f"{run.name} finished in {run.seconds:.0f} s"
    tail = run.output.rstrip("\n").splitlines()[-TAIL_LINES:]
    where = f"Log: {log}" if log else "Pass --out to keep its whole log"
    return f"{how}. {where}\n" + "".join(f"  {line}\n" for line in tail)


#: /proc's one-letter thread states, from proc_pid_stat(5).
THREAD_STATES = {
    "R": "running",
    "S": "sleeping",
    "D": "in uninterruptible sleep",
    "T": "stopped",
    "t": "stopped by a debugger",
    "Z": "a zombie",
    "I": "idle",
}


def find_process(root: int, name: str, run: Runner) -> int | None:
    """The process named name, root itself or its first descendant.

    It finds fun-ofdm under `sudo`, which can fork once or twice.

    Args:
        root: The process to start from.
        name: The name to find. /proc truncates names to 15 characters.
        run: Reads /proc with `cat`, with sudo if the process is root's.
    """
    pending = [root]
    while pending:
        pid = pending.pop(0)
        if (run(["cat", f"/proc/{pid}/comm"]) or "").strip() == name[:15]:
            return pid
        children = run(["cat", f"/proc/{pid}/task/{pid}/children"]) or ""
        pending += [int(child) for child in children.split()]
    return None


def thread_states(pid: int, run: Runner) -> str:
    """Where each of a process's threads is, from /proc, to explain a hang.

    Each thread's ID, name and state, and the kernel function it waits in
    (wchan), then its kernel stack, which only root can read.

    Args:
        pid: The process.
        run: Reads /proc with `ls` and `cat`, with sudo if the process is
            root's.
    """
    task = f"/proc/{pid}/task"
    listing = run(["ls", task])
    if listing is None:
        return f"{task} can't be read: the process has exited.\n"
    lines = []
    for tid in sorted(listing.split(), key=int):
        path = f"{task}/{tid}"
        name = (run(["cat", f"{path}/comm"]) or "?").strip()
        # The state follows the name, which is in parentheses
        stat = (run(["cat", f"{path}/stat"]) or "").rpartition(")")[2].split()
        state = THREAD_STATES.get(stat[0], stat[0]) if stat else "?"
        wchan = (run(["cat", f"{path}/wchan"]) or "").strip()
        waiting = f", waiting in {wchan}" if wchan not in ("", "0") else ""
        lines.append(f"{tid} {name}: {state}{waiting}")
        stack = run(["cat", f"{path}/stack"]) or ""
        lines += [f"    {frame}" for frame in stack.splitlines()]
    return "\n".join(lines) + "\n"


#: The columns of loopback_row().
LOOPBACK_COLUMNS = (
    "Sent",
    "Received",
    "Lost",
    "Duplicates",
    "Bytes wrong",
    "Overflows",
    "Mbit/s",
)


def run_row(run: Run) -> dict[str, str]:
    """One row of result.md: how a run ended, then what its summary says."""
    return {
        "Run": run.name,
        "Status": run.status,
        "Time (s)": f"{run.seconds:.0f}",
        **loopback_row(run.summary),
    }


def loopback_row(summary: dict[str, Any] | None) -> dict[str, str]:
    """The LOOPBACK_COLUMNS of a `fun-ofdm loopback --json` summary.

    Each is "?" without a summary.
    """
    if summary is None:
        return dict.fromkeys(LOOPBACK_COLUMNS, "?")
    metrics = summary["metrics"]
    received = summary["received"]
    bits_per_second = received["bits_per_second"]
    return {
        "Sent": str(summary["sent"]["packets"]),
        "Received": str(received["packets"]),
        "Lost": str(summary.get("lost", "?")),
        "Duplicates": str(summary.get("duplicates", "?")),
        "Bytes wrong": str(summary.get("mismatched_bytes", "?")),
        "Overflows": str(metrics["overflows"]),
        "Mbit/s": f"{bits_per_second / 1e6:.2f}"
        if bits_per_second is not None
        else "?",
    }


def markdown_table(rows: Sequence[dict[str, str]]) -> str:
    """Rows with the same keys as a Markdown table, the keys as its header.

    Each column is padded to its widest cell, so the table lines up as text
    too, like in the terminal.
    """
    if not rows:
        return ""
    header = list(rows[0])
    widths = [max(len(key), *(len(row[key]) for row in rows)) for key in header]

    def line(cells: Sequence[str]) -> str:
        padded = (
            cell.ljust(width) for cell, width in zip(cells, widths, strict=True)
        )
        return "| " + " | ".join(padded) + " |"

    lines = [line(header), line(["-" * width for width in widths])]
    lines += [line([row[key] for key in header]) for row in rows]
    return "\n".join(lines) + "\n"


def results_markdown(
    title: str, meta: dict[str, Any], rows: Sequence[dict[str, str]]
) -> str:
    """result.md: a heading, what the runs ran on, and a table of the runs.

    Args:
        title: The test, for the heading.
        meta: What metadata() returns.
        rows: The table, the rows to paste into the release notes.
    """
    settings = ", ".join(f"{k} {v}" for k, v in meta["settings"].items())
    usrps = ", ".join(f"{u['product']} {u['serial']}" for u in meta["usrps"])
    commit = (meta["host"].get("commit") or "unknown")[:12]
    dirty = " (with uncommitted changes)" if meta["host"].get("dirty") else ""
    return (
        f"# {title}\n\n"
        f"{usrps}; {settings}; UHD {meta['uhd_version']}; commit {commit}{dirty}"
        "\n\n" + markdown_table(rows)
    )


def write_results(
    out: pathlib.Path,
    title: str,
    meta: dict[str, Any],
    runs: dict[str, Any],
    rows: Sequence[dict[str, str]],
) -> None:
    """Writes result.json (everything) and result.md (the rows) to out.

    Args:
        out: The directory, which is created if needed.
        title: The test, for result.md's heading.
        meta: What metadata() returns.
        runs: The raw results, by name, like each run's JSON summary.
        rows: result.md's table, the rows to paste into the release notes.
    """
    out.mkdir(parents=True, exist_ok=True)
    (out / "result.json").write_text(
        json.dumps({**meta, "runs": runs}, indent=2) + "\n"
    )
    (out / "result.md").write_text(results_markdown(title, meta, rows))
