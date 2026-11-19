"""What the hardware tests in hil/ share: the topologies, UHD's output, results.

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

#: The least attenuation allowed between a transmitter and a receiver.
MIN_ATTENUATION_DB = 30

#: Runs a command and returns its stdout, or None if it can't be run or
#: fails.
Runner = Callable[[Sequence[str]], str | None]


@dataclasses.dataclass(frozen=True)
class Topology:
    """How the radios are connected, which a run names with --topology."""

    #: Its name for --topology, like "loopback".
    name: str
    #: What it is, in a few words.
    description: str
    #: How many USRPs it needs.
    usrps: int
    #: The wiring, drawn in text.
    diagram: str
    #: What to do to build it, one step at a time.
    steps: tuple[str, ...]


#: The standard topologies, by name.
TOPOLOGIES = {
    "loopback": Topology(
        name="loopback",
        description="one B210, from its channel A to its channel B",
        usrps=1,
        diagram="""\
  ┌────────────────────── B210 ──────────────────────┐
  │   RF A: TX/RX   RX2            RF B: TX/RX   RX2  │
  └─────────┬──────────────────────────┬─────────────┘
            │                          │
            └──── 30-40 dB attenuator ─┘""",
        steps=(
            (
                "Connect a 30 to 40 dB attenuator between the TX/RX ports of "
                "RF A and RF B."
            ),
            (
                "Plug the B210 into a USB 3 port (blue on a Raspberry Pi 5), "
                "with its power supply if you have one."
            ),
        ),
    ),
    "cabled": Topology(
        name="cabled",
        description="two B210s, cabled to each other",
        usrps=2,
        diagram="""\
  ┌─ B210 #1 ─┐                          ┌─ B210 #2 ─┐
  │  TX/RX A ─┼── 30-40 dB attenuator ───┼─ TX/RX A  │
  └───────────┘                          └───────────┘""",
        steps=(
            (
                "Connect a 30 to 40 dB attenuator between the TX/RX A ports "
                "of the two B210s."
            ),
            "Connect both B210s to USB 3 ports.",
        ),
    ),
}

#: The rules every topology follows, which --interactive prints first.
SAFETY = """\
- Never cable TX straight into RX: always use at least 30 dB of attenuation,
  and start with low TX gain.
- A B210 running both channels may need its external supply."""


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
        settings: The radio settings: frequency, gains, attenuation.
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


#: Batches of samples arriving further apart than this, in ns, mean the USRP
#: isn't streaming: a batch takes 0.8 ms at 5 MS/s, and UHD waits 100 ms.
MAX_ARRIVAL_INTERVAL_NS = 50_000_000


def streaming(summary: dict[str, Any]) -> bool:
    """Whether the USRP streamed, from a --json summary.

    A stalled USRP still opens, so the only sign is that each batch of samples
    takes UHD's whole timeout.
    """
    arrival = summary["metrics"]["arrival_interval"]
    return arrival["count"] > 0 and arrival["p50"] < MAX_ARRIVAL_INTERVAL_NS


def loopback_row(summary: dict[str, Any]) -> dict[str, str]:
    """One row of result.md, from a `fun-ofdm loopback --json` summary."""
    metrics = summary["metrics"]
    arrival = metrics["arrival_interval"]
    received = summary["received"]
    bits_per_second = received["bits_per_second"]
    return {
        "Rate": summary["options"]["phy_rate"],
        "Sent": str(summary["sent"]["packets"]),
        "Received": str(received["packets"]),
        "Lost": str(summary.get("lost", "?")),
        "Duplicates": str(summary.get("duplicates", "?")),
        "Bytes wrong": str(summary.get("mismatched_bytes", "?")),
        "Overflows": str(metrics["overflows"]),
        "False detections": str(metrics["frames"]["sync_lost"]),
        "Sample arrival p50 / p99 / max (µs)": " / ".join(
            f"{arrival[key] / 1e3:.0f}" for key in ("p50", "p99", "max")
        ),
        "Mbit/s": f"{bits_per_second / 1e6:.2f}"
        if bits_per_second is not None
        else "?",
    }


def markdown_table(rows: Sequence[dict[str, str]]) -> str:
    """Rows with the same keys as a Markdown table, the keys as its header."""
    if not rows:
        return ""
    header = list(rows[0])
    lines = [
        "| " + " | ".join(header) + " |",
        "|" + "|".join("---" for _ in header) + "|",
    ]
    lines += [
        "| " + " | ".join(row[key] for key in header) + " |" for row in rows
    ]
    return "\n".join(lines) + "\n"


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
    settings = ", ".join(f"{k} {v}" for k, v in meta["settings"].items())
    usrps = ", ".join(f"{u['product']} {u['serial']}" for u in meta["usrps"])
    commit = (meta["host"].get("commit") or "unknown")[:12]
    (out / "result.md").write_text(
        f"# {title}\n\n"
        f"{usrps}; {settings}; UHD {meta['uhd_version']}; "
        f"commit {commit}"
        f"{' (with uncommitted changes)' if meta['host'].get('dirty') else ''}"
        "\n\n" + markdown_table(rows)
    )
