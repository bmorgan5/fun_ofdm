"""The result contract: what every measurement tool writes with --out DIR.

Each tool writes two files to its directory:

- result.json: an object whose first member is `provenance`, everything the
  result depends on (provenance()), then the tool's own results.
- result.md: a heading, a line saying what made the result, then the tool's
  tables, for the scorecard and the release notes.

A result can stand in for a new measurement when its provenance matches what
would run now (reuse_problems()): `uv run tools/results/result.py DIR...`
says which can.
"""

import argparse
import datetime
import hashlib
import json
import os
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "scorecard"))

import hostcheck

#: The binaries a result can depend on, which `bazel run install` copies to
#: the same directory.
BINARIES = ("fun-ofdm", "fun-ofdm-bench")

#: The host's fields that a reused result must share: what the machine is,
#: and how it's set up. The load, the throttling, the warnings, the commit
#: and the binary's version aren't: the binaries' hashes cover the code.
HOST_FIELDS = (
    "cpu_model",
    "board",
    "arch",
    "kernel",
    "kernel_build",
    "preempt_rt",
    "virtualization",
    "container",
    "cpus_online",
    "cpus_usable",
    "cpu_quota",
    "governors",
    "turbo",
    "isolated",
    "nohz_full",
    "configuration",
)


def sha256(path: pathlib.Path) -> str:
    """A file's SHA-256, in hex."""
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def jsonable(value: Any) -> Any:
    """A tool's option as JSON: paths become strings, and tuples lists."""
    if isinstance(value, pathlib.Path):
        return str(value)
    if isinstance(value, list | tuple):
        return [jsonable(item) for item in value]
    if isinstance(value, dict):
        return {key: jsonable(item) for key, item in value.items()}
    return value


def provenance(
    tool: str,
    options: Mapping[str, Any],
    binaries: Mapping[str, pathlib.Path],
    hardware: Mapping[str, Any] | None = None,
    host: hostcheck.Host | None = None,
    sources: Sequence[pathlib.Path] = (),
) -> dict[str, Any]:
    """Everything a result depends on, for the start of its result.json.

    Args:
        tool: The tool, like "tools/per/sweep.py".
        options: The options that change what it measures, so not where it
            writes, or how many jobs it runs at once. A binary's path isn't
            one: its SHA-256 is.
        binaries: Each binary it ran, by name, like {"fun-ofdm": path}.
        hardware: For a tool that uses a USRP: which one, the ports, gains,
            channel and bandwidth.
        host: The machine, by default hostcheck's report on it now, with
            fun-ofdm's compiler and version.
        sources: The tool's own Python files, which change what it measures
            as much as the binaries do.

    Returns:
        The tool and its options, when it ran, each binary's path and
        SHA-256, each source's SHA-256 by its path in the repo, the host,
        and the hardware or None. Take it before the tool runs: it describes
        what's about to be measured.
    """
    if host is None:
        fun_ofdm = binaries.get("fun-ofdm", hostcheck.DEFAULT_FUN_OFDM)
        host = hostcheck.collect(fun_ofdm=fun_ofdm)
    return {
        "tool": tool,
        "options": jsonable(dict(options)),
        "date": datetime.datetime.now(datetime.UTC).isoformat(
            timespec="seconds"
        ),
        "binaries": {
            name: {"path": str(path), "sha256": sha256(path)}
            for name, path in binaries.items()
        },
        "sources": {source_name(path): sha256(path) for path in sources},
        "host": hostcheck.to_json(host),
        "hardware": jsonable(dict(hardware)) if hardware is not None else None,
    }


def source_name(path: pathlib.Path) -> str:
    """A source's path in the repo, or its full path outside it."""
    path = path.resolve()
    return (
        str(path.relative_to(REPO)) if path.is_relative_to(REPO) else str(path)
    )


def describe(prov: Mapping[str, Any]) -> str:
    """A line of Markdown saying what made a result, for result.md."""
    host = prov["host"]
    machine = host.get("board") or host.get("cpu_model") or host.get("arch")
    commit = (host.get("commit") or "unknown")[:12]
    dirty = " with uncommitted changes" if host.get("dirty") else ""
    binaries = ", ".join(
        f"{name} {binary['sha256'][:12]}"
        for name, binary in prov["binaries"].items()
    )
    parts = [
        f"{prov['date']}",
        (
            f"fun-ofdm {host.get('fun_ofdm_version') or 'unknown'} at commit "
            f"{commit}{dirty}"
        ),
        f"SHA-256 {binaries}" if binaries else "no binaries",
        f"{machine}, {host.get('configuration')}",
    ]
    if prov.get("hardware"):
        parts.append(
            ", ".join(
                f"{key} {value}" for key, value in prov["hardware"].items()
            )
        )
    return "; ".join(parts) + "."


def write(
    out: pathlib.Path,
    prov: Mapping[str, Any],
    results: Mapping[str, Any],
    title: str,
    body: str,
) -> None:
    """Writes result.json and result.md to a directory, creating it.

    Args:
        out: The directory.
        prov: What provenance() returns.
        results: The tool's results, which follow the provenance.
        title: result.md's heading.
        body: result.md's Markdown after the heading and provenance line.
    """
    out.mkdir(parents=True, exist_ok=True)
    (out / "result.json").write_text(
        json.dumps({"provenance": prov, **results}, indent=2) + "\n"
    )
    (out / "result.md").write_text(markdown(prov, title, body))


def markdown(prov: Mapping[str, Any], title: str, body: str) -> str:
    """result.md: the heading, the provenance line, then the body."""
    return f"# {title}\n\n{describe(prov)}\n\n{body.rstrip()}\n"


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


def read(out: pathlib.Path) -> dict[str, Any]:
    """A directory's result.json."""
    return json.loads((out / "result.json").read_text())


def reuse_problems(old: Mapping[str, Any], new: Mapping[str, Any]) -> list[str]:
    """Why a result can't stand in for a new one, or [] if it can.

    Args:
        old: The result's provenance.
        new: The provenance of what would run now. Its tool, options and
            hardware are compared only if it has them.

    Returns:
        Each difference: a binary or source that changed, the host's setup,
        or the tool, its options or the hardware.
    """
    problems = []
    for name, digest in new.get("sources", {}).items():
        if old.get("sources", {}).get(name) != digest:
            problems.append(f"{name} has changed")
    for name, binary in new["binaries"].items():
        before = old["binaries"].get(name)
        if before is None:
            problems.append(f"it didn't run {name}")
        elif before["sha256"] != binary["sha256"]:
            problems.append(f"{name} has changed")
    for field in HOST_FIELDS:
        if old["host"].get(field) != new["host"].get(field):
            problems.append(
                f"the host's {field} was {old['host'].get(field)!r}, and is "
                f"now {new['host'].get(field)!r}"
            )
    for key in ("tool", "options", "hardware"):
        if key in new and old.get(key) != new[key]:
            problems.append(f"its {key} differ")
    return problems


def main(argv: Sequence[str] | None = None) -> int:
    """Says which results could be reused on this machine, with its binaries.

    It compares each result's binaries, sources and host with this
    machine's, not its tool's options or hardware, which the person
    measuring decides.
    """
    parser = argparse.ArgumentParser(
        description="Says which results could be reused on this machine.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "dirs", nargs="+", type=pathlib.Path, help="result directories"
    )
    parser.add_argument(
        "--bin-dir",
        type=pathlib.Path,
        default=hostcheck.DEFAULT_FUN_OFDM.parent,
        help="where the binaries that would run now are",
    )
    parser.add_argument(
        "--cpus",
        help="the CPUs the tools would be pinned to, like 1-3 (default: "
        "this process's)",
    )
    args = parser.parse_args(argv)

    if args.cpus is not None:
        os.sched_setaffinity(0, hostcheck.parse_cpu_list(args.cpus))
    olds = {}
    unreadable = []
    for out in args.dirs:
        try:
            olds[out] = read(out)["provenance"]
        except OSError, ValueError, KeyError:
            unreadable.append(out)
    names = {name for old in olds.values() for name in old["binaries"]}
    binaries = {name: args.bin_dir / name for name in sorted(names)}
    missing = [str(path) for path in binaries.values() if not path.exists()]
    if missing:
        parser.error(f"{', '.join(missing)} missing: bazel run install")
    fun_ofdm = args.bin_dir / "fun-ofdm"
    host = hostcheck.collect(
        fun_ofdm=fun_ofdm if fun_ofdm.exists() else hostcheck.DEFAULT_FUN_OFDM
    )
    now = provenance("", {}, binaries, host=host)
    del now["tool"], now["options"], now["hardware"], now["sources"]
    reusable = not unreadable
    for out in unreadable:
        print(f"{out}: measure again: it has no result.json with provenance")
    for out, old in olds.items():
        # Only the binaries and sources it ran
        compared = {
            **now,
            "binaries": {
                name: binary
                for name, binary in now["binaries"].items()
                if name in old["binaries"]
            },
            "sources": {
                name: sha256(REPO / name) if (REPO / name).exists() else ""
                for name in old.get("sources", {})
            },
        }
        problems = reuse_problems(old, compared)
        reusable &= not problems
        unchecked = " (not its hardware)" if old.get("hardware") else ""
        if problems:
            print(f"{out}: measure again: " + "; ".join(problems))
        else:
            print(f"{out}: can be reused{unchecked}")
    return 0 if reusable else 1


if __name__ == "__main__":
    sys.exit(main())
