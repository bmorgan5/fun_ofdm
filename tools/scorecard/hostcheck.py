"""Records the machine a scorecard runs on, and warns about what skews it.

A benchmark only means something next to the machine that ran it: the CPU,
the kernel, how the CPUs are clocked, and what else they're doing. This
records them, names the tuning configuration they add up to (Stock, Pinned,
Isolated or RT; see README.md), and warns about anything that would make the
numbers noisy or unfair to compare.

    uv run tools/scorecard/hostcheck.py
    uv run tools/scorecard/hostcheck.py --json

scorecard.py runs it first and puts its report in every result file.
"""

import argparse
import collections
import dataclasses
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
from collections.abc import Callable, Sequence
from typing import Any

REPO = pathlib.Path(__file__).resolve().parents[2]

#: The fun-ofdm binary that `bazel run install` copies, for this machine.
DEFAULT_FUN_OFDM = (
    REPO / "out" / f"linux_{platform.machine()}" / "bin" / "fun-ofdm"
)

#: The tuning configurations, each adding to the one before (README.md).
CONFIGURATIONS = ("Stock", "Pinned", "Isolated", "RT")

#: The threads the receiver runs: its six blocks and the caller's.
RECEIVER_THREADS = 7

#: The 1-minute load average above which the machine isn't idle.
MAX_LOAD = 0.5

#: The ARM cores the project is likely to run on, by /proc/cpuinfo's
#: "CPU part" (implementer 0x41, ARM), as in src/bench/host_info.cpp.
ARM_CORES = {
    "0xd03": "Cortex-A53",
    "0xd05": "Cortex-A55",
    "0xd08": "Cortex-A72",
    "0xd0b": "Cortex-A76",
    "0xd0c": "Neoverse-N1",
    "0xd40": "Neoverse-V1",
    "0xd49": "Neoverse-N2",
    "0xd4f": "Neoverse-V2",
}

#: Runs a command and returns its stdout, or None if it can't be run or
#: fails.
Runner = Callable[[Sequence[str]], str | None]


def run_command(command: Sequence[str]) -> str | None:
    """Runs a command, returning its stdout, or None if it fails."""
    try:
        done = subprocess.run(
            command, check=False, capture_output=True, text=True
        )
    except OSError:
        return None
    return done.stdout if done.returncode == 0 else None


@dataclasses.dataclass
class Host:
    """What hostcheck records about a machine."""

    cpu_model: str
    #: The board, from the device tree, like "Raspberry Pi 5 Model B Rev 1.0",
    #: or "" without one.
    board: str
    arch: str
    kernel: str
    #: The kernel's build string (uname -v), which names PREEMPT_RT.
    kernel_build: str
    preempt_rt: bool
    #: "wsl", "vm" or "none": a hypervisor shares the CPUs with others.
    virtualization: str
    #: "docker", "podman" or "none".
    container: str
    cpus_online: int
    #: The CPUs this process may run on, its affinity, which taskset or
    #: scorecard.py's --cpus sets.
    cpus_usable: list[int]
    #: The cgroup's CPU limit, like 2.5 CPUs, or None for none.
    cpu_quota: float | None
    #: How many CPUs use each frequency governor; empty if there's no
    #: cpufreq, as in a VM.
    governors: dict[str, int]
    #: Whether turbo (Intel) or boost (AMD) is on, or None if there's no such
    #: setting.
    turbo: bool | None
    #: The kernel's isolated CPUs (isolcpus) and tickless ones (nohz_full),
    #: in its list format, like "2-5", or "".
    isolated: str
    nohz_full: str
    load_average: float
    #: The Raspberry Pi's throttling flags (vcgencmd get_throttled), like
    #: "0x0", or None off a Pi.
    throttled: str | None
    on_battery: bool
    #: The compiler that built fun-ofdm, from its .comment section, and its
    #: version.
    compiler: str | None
    fun_ofdm_version: str | None
    commit: str | None
    #: Whether the work tree has uncommitted changes to tracked files.
    dirty: bool
    configuration: str = "Stock"
    warnings: list[str] = dataclasses.field(default_factory=list)


def text(path: pathlib.Path) -> str:
    """A file's text, stripped, or "" if it can't be read."""
    try:
        return path.read_text(errors="replace").strip()
    except OSError:
        return ""


def read(root: pathlib.Path, path: str) -> str:
    """The text of a file under root, like /proc, stripped, or ""."""
    return text(root / path.lstrip("/"))


def cpuinfo_value(cpuinfo: str, key: str) -> str:
    """The value of the first "key : value" line in /proc/cpuinfo's text."""
    for line in cpuinfo.splitlines():
        name, colon, value = line.partition(":")
        if colon and name.strip() == key:
            return value.strip()
    return ""


def cpu_model(cpuinfo: str) -> str:
    """The CPU's name, from /proc/cpuinfo's text.

    x86 names its model; ARM only numbers its implementer and part.
    """
    model = cpuinfo_value(cpuinfo, "model name")
    if model:
        return model
    implementer = cpuinfo_value(cpuinfo, "CPU implementer")
    part = cpuinfo_value(cpuinfo, "CPU part")
    if not implementer or not part:
        return ""
    if implementer == "0x41" and part in ARM_CORES:
        return f"ARM {ARM_CORES[part]}"
    return f"CPU implementer {implementer}, part {part}"


def parse_cpu_list(text: str) -> list[int]:
    """The CPUs in the kernel's list format, like "0-3,8"."""
    cpus: list[int] = []
    for part in text.replace("\n", ",").split(","):
        part = part.strip()
        if not part:
            continue
        first, _, last = part.partition("-")
        cpus.extend(range(int(first), int(last or first) + 1))
    return cpus


def format_cpu_list(cpus: Sequence[int]) -> str:
    """CPUs in the kernel's list format, with runs as ranges: "0-3,8"."""
    parts: list[str] = []
    ordered = sorted(set(cpus))
    start = 0
    for i in range(1, len(ordered) + 1):
        if i == len(ordered) or ordered[i] != ordered[i - 1] + 1:
            first, last = ordered[start], ordered[i - 1]
            parts.append(str(first) if first == last else f"{first}-{last}")
            start = i
    return ",".join(parts)


def cpu_quota(root: pathlib.Path) -> float | None:
    """The process's CPU quota: the smallest of its cgroup's and parents'.

    /proc/self/cgroup's "0::<path>" line names its cgroup v2 group, under
    /sys/fs/cgroup, whose cpu.max is "<quota> <period>" or "max <period>".
    """
    group = ""
    for line in read(root, "/proc/self/cgroup").splitlines():
        if line.startswith("0::"):
            group = line[3:]
    if not group:
        return None
    quota = None
    path = pathlib.PurePosixPath(group)
    for directory in [path, *path.parents]:
        fields = read(root, f"/sys/fs/cgroup{directory}/cpu.max").split()
        if len(fields) == 2 and fields[0] != "max" and float(fields[1]) > 0:
            limit = float(fields[0]) / float(fields[1])
            quota = limit if quota is None else min(quota, limit)
    return quota


def governors(root: pathlib.Path) -> dict[str, int]:
    """How many CPUs use each frequency governor."""
    counts = collections.Counter(
        text(governor)
        for governor in sorted(
            (root / "sys/devices/system/cpu").glob(
                "cpu[0-9]*/cpufreq/scaling_governor"
            )
        )
    )
    return dict(sorted(counts.items()))


def turbo(root: pathlib.Path) -> bool | None:
    """Whether turbo is on: Intel's intel_pstate, or cpufreq's boost."""
    no_turbo = read(root, "/sys/devices/system/cpu/intel_pstate/no_turbo")
    if no_turbo:
        return no_turbo == "0"
    boost = read(root, "/sys/devices/system/cpu/cpufreq/boost")
    if boost:
        return boost == "1"
    return None


def virtualization(root: pathlib.Path, kernel: str) -> str:
    """Whether a hypervisor runs this machine: "wsl", "vm" or "none"."""
    if "microsoft" in kernel.lower():
        return "wsl"
    flags = cpuinfo_value(read(root, "/proc/cpuinfo"), "flags").split()
    if "hypervisor" in flags or read(root, "/sys/hypervisor/type"):
        return "vm"
    return "none"


def container(root: pathlib.Path) -> str:
    """The container runtime this runs in: "docker", "podman" or "none"."""
    if (root / ".dockerenv").exists():
        return "docker"
    if (root / "run/.containerenv").exists():
        return "podman"
    return "none"


def on_battery(root: pathlib.Path) -> bool:
    """Whether a laptop's mains power is unplugged."""
    for supply in sorted((root / "sys/class/power_supply").glob("*")):
        if text(supply / "type") == "Mains":
            return text(supply / "online") == "0"
    return False


def compiler(fun_ofdm: pathlib.Path) -> str | None:
    """The compiler that built a binary, from the strings in its .comment."""
    try:
        data = fun_ofdm.read_bytes()
    except OSError:
        return None
    # Clang first: a clang binary also has GCC's string, from the C++
    # runtime's objects it links.
    match = re.search(rb"clang version [\w.\-]+", data) or re.search(
        rb"GCC: \([^)]*\) [\w.\-]+", data
    )
    return match.group().decode() if match else None


def configuration(host: Host) -> str:
    """The tuning configuration the machine is in (README.md).

    Each needs everything the one before it does:

    - Pinned: the process pinned to some CPUs, every CPU on the performance
      governor, and turbo off.
    - Isolated: the pinned CPUs are isolated and tickless.
    - RT: a PREEMPT_RT kernel.
    """
    pinned = (
        len(host.cpus_usable) < host.cpus_online
        and set(host.governors) == {"performance"}
        and not host.turbo
    )
    if not pinned:
        return "Stock"
    isolated = set(parse_cpu_list(host.isolated))
    tickless = set(parse_cpu_list(host.nohz_full))
    usable = set(host.cpus_usable)
    if not (usable <= isolated and usable <= tickless):
        return "Pinned"
    return "RT" if host.preempt_rt else "Isolated"


def warnings(host: Host) -> list[str]:
    """What would make the numbers noisy, or unfair to compare."""
    found: list[str] = []
    if host.virtualization != "none":
        name = "WSL" if host.virtualization == "wsl" else "a VM"
        found.append(
            f"This runs in {name}: the hypervisor shares the CPUs with other "
            "work, and the governor and turbo can't be set from inside it, "
            "so the numbers are noisy. Use them to compare runs on this "
            "machine, not as the scorecard's numbers."
        )
    if host.container != "none" and host.cpu_quota is not None:
        found.append(
            f"The container may use only {host.cpu_quota:g} CPUs (a cgroup "
            "quota, like Docker's --cpus), which throttles the receiver."
        )
    if len(host.cpus_usable) < RECEIVER_THREADS:
        found.append(
            f"This process may use only {len(host.cpus_usable)} CPUs, and "
            f"the receiver runs {RECEIVER_THREADS} threads, so they take "
            "turns."
        )
    if not host.governors:
        if host.virtualization == "none":
            found.append(
                "The CPUs' frequency governor can't be read (no cpufreq)."
            )
    elif set(host.governors) != {"performance"}:
        others = ", ".join(
            f"{name} on {count}"
            for name, count in host.governors.items()
            if name != "performance"
        )
        found.append(
            f"Not every CPU uses the performance governor ({others}), so "
            "clocks change during a run: "
            "echo performance | sudo tee "
            "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor"
        )
    if host.turbo:
        found.append(
            "Turbo is on, so the clock depends on temperature and on how "
            "many cores are busy: "
            + (
                "echo 1 | sudo tee "
                "/sys/devices/system/cpu/intel_pstate/no_turbo"
                if "Intel" in host.cpu_model
                else "echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost"
            )
        )
    if host.load_average > MAX_LOAD:
        found.append(
            f"The machine isn't idle: its load average is "
            f"{host.load_average:.2f}. Close other work first."
        )
    if host.isolated and not set(host.cpus_usable) <= set(
        parse_cpu_list(host.isolated)
    ):
        found.append(
            f"CPUs {host.isolated} are isolated, but this process may also "
            f"run on others ({format_cpu_list(host.cpus_usable)}). Pin it "
            f"to them: taskset -c {host.isolated}, or scorecard.py's --cpus "
            f"{host.isolated}."
        )
    if host.throttled not in (None, "0x0"):
        found.append(
            f"The Pi has been throttled (vcgencmd get_throttled: "
            f"{host.throttled}), by heat or by low voltage."
        )
    if host.on_battery:
        found.append("The laptop is on battery, which can lower its clocks.")
    if host.commit is None:
        found.append("The commit isn't known: this isn't a git checkout.")
    elif host.dirty:
        found.append(
            "The work tree has uncommitted changes, so the commit doesn't "
            "say exactly what ran."
        )
    if host.compiler is None:
        found.append("The compiler that built fun-ofdm isn't known.")
    return found


def collect(
    root: pathlib.Path = pathlib.Path("/"),
    fun_ofdm: pathlib.Path = DEFAULT_FUN_OFDM,
    *,
    uname: os.uname_result | None = None,
    affinity: set[int] | None = None,
    run: Runner = run_command,
) -> Host:
    """Records the machine.

    Args:
        root: Where /proc and /sys are, which tests replace.
        fun_ofdm: The binary to record the compiler and version of.
        uname: The kernel's names, by default os.uname().
        affinity: The CPUs this process may run on, by default its own.
        run: Runs git, vcgencmd and fun-ofdm.

    Returns:
        The machine, with its configuration and warnings.
    """
    names = uname or os.uname()
    cpuinfo = read(root, "/proc/cpuinfo")
    usable = sorted(
        affinity if affinity is not None else os.sched_getaffinity(0)
    )
    online = parse_cpu_list(read(root, "/sys/devices/system/cpu/online"))
    throttled = run(["vcgencmd", "get_throttled"])
    version = run([str(fun_ofdm), "--version"])
    commit = run(["git", "-C", str(REPO), "rev-parse", "HEAD"])
    status = run(["git", "-C", str(REPO), "status", "--porcelain", "-uno"])
    load = read(root, "/proc/loadavg").split()
    host = Host(
        cpu_model=cpu_model(cpuinfo),
        board=read(root, "/sys/firmware/devicetree/base/model").rstrip("\0"),
        arch=names.machine,
        kernel=names.release,
        kernel_build=names.version,
        preempt_rt=read(root, "/sys/kernel/realtime") == "1"
        or "PREEMPT_RT" in names.version,
        virtualization=virtualization(root, names.release),
        container=container(root),
        cpus_online=len(online) or len(usable),
        cpus_usable=usable,
        cpu_quota=cpu_quota(root),
        governors=governors(root),
        turbo=turbo(root),
        isolated=read(root, "/sys/devices/system/cpu/isolated"),
        nohz_full=read(root, "/sys/devices/system/cpu/nohz_full"),
        load_average=float(load[0]) if load else 0.0,
        throttled=throttled.strip().removeprefix("throttled=")
        if throttled
        else None,
        on_battery=on_battery(root),
        compiler=compiler(fun_ofdm),
        fun_ofdm_version=version.strip() if version else None,
        commit=commit.strip() if commit else None,
        dirty=bool(status and status.strip()),
    )
    host.configuration = configuration(host)
    host.warnings = warnings(host)
    return host


def report(host: Host) -> str:
    """The machine as text, for a person: what it is, then the warnings."""
    governor_text = (
        ", ".join(f"{n} on {c}" for n, c in host.governors.items()) or "unknown"
    )
    turbo_text = {True: "on", False: "off", None: "no setting"}[host.turbo]
    rows = [
        ("CPU", host.cpu_model + (f" ({host.board})" if host.board else "")),
        ("Kernel", host.kernel + (" (PREEMPT_RT)" if host.preempt_rt else "")),
        ("Hypervisor", host.virtualization),
        ("Container", host.container),
        (
            "CPUs usable",
            f"{format_cpu_list(host.cpus_usable)} of {host.cpus_online}",
        ),
        ("Governor", governor_text),
        ("Turbo", turbo_text),
        ("Isolated CPUs", host.isolated or "none"),
        ("Load average", f"{host.load_average:.2f}"),
        ("Compiler", host.compiler or "unknown"),
        (
            "Commit",
            (host.commit or "unknown")[:12]
            + (" (uncommitted changes)" if host.dirty else ""),
        ),
        ("Configuration", host.configuration),
    ]
    width = max(len(name) for name, _ in rows)
    lines = [f"{name:<{width}}  {value}" for name, value in rows]
    if host.warnings:
        lines += ["", "Warnings:"]
        lines += [f"- {warning}" for warning in host.warnings]
    return "\n".join(lines)


def to_json(host: Host) -> dict[str, Any]:
    """The machine as JSON."""
    return dataclasses.asdict(host)


def main(argv: Sequence[str] | None = None) -> int:
    """Prints the machine's report, or its JSON."""
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--fun-ofdm",
        type=pathlib.Path,
        default=DEFAULT_FUN_OFDM,
        help="the binary to record the compiler of",
    )
    parser.add_argument("--json", action="store_true", help="print JSON")
    args = parser.parse_args(argv)

    host = collect(fun_ofdm=args.fun_ofdm)
    print(json.dumps(to_json(host), indent=2) if args.json else report(host))
    return 0


if __name__ == "__main__":
    sys.exit(main())
