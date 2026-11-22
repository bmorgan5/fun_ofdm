"""Sweeps on hardware, over the B210 loopback in hil/README.md.

    uv run tools/hardware/hardware_sweep.py realtime --out realtime/

- `realtime`: the highest sample rate the receiver keeps up with. At each of
  802.11a's bandwidths, 5, 10 and 20 MHz, which are also the sample rates, it
  sends frames at BPSK 1/2, the lightest rate to decode, and at QAM64 3/4, the
  heaviest, and receives nothing but noise (`idle`), the receiver's floor,
  several times each.

Each step records the USRP's overflows, the frames lost, the latency, each
block's processing time, and the CPU use of each core, sampled from
/proc/stat. With --out DIR, it writes them as tools/results/result.py says.
"""

import argparse
import dataclasses
import logging
import pathlib
import statistics
import sys
import threading
from collections.abc import Callable, Sequence
from typing import Any, Self

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "results"))
sys.path.insert(0, str(REPO / "tools" / "scorecard"))

import hardware
import result

log = logging.getLogger("sweep")

#: The bandwidths realtime steps through, in MHz: also the sample rates.
#: 802.11a's channel widths: 20 MHz, and its half and quarter rates.
BANDWIDTHS_MHZ = (5.0, 10.0, 20.0)

#: What the receiver gets in each realtime step: frames at the lightest and
#: the heaviest rate to decode, or only noise.
IDLE = "idle"
WORKLOADS = ("bpsk-1/2", "qam64-3/4", IDLE)

#: How long each realtime step sends frames or receives noise, in seconds.
STEP_SECONDS = 5.0

#: How many times realtime runs each step.
REPETITIONS = 3

#: How long fun-ofdm may take to open the USRP, in a step's time limit.
RUN_START_S = 30

#: What loopback adds to each frame's time on air: it waits for the USRP to
#: acknowledge each one. A step's packet count allows FRAME_GAP_S a frame,
#: about what loopback takes, and its time limit FRAME_LIMIT_GAP_S, which
#: only a run that's stuck takes.
FRAME_GAP_S = 0.0005
FRAME_LIMIT_GAP_S = 0.005

#: An idle step stops rx this long plus the step's seconds after it starts.
#: Opening the USRP usually takes about 3 s, so it receives noise for longer
#: than the step's seconds, which can only find more overflows.
IDLE_START_S = 10

#: How often the CPU use is sampled, in seconds.
CPU_SAMPLE_S = 0.5

#: The receiver's blocks, in order (src/phy/receiver_chain.h).
BLOCKS = (
    "frame_detector",
    "timing_sync",
    "fft_symbols",
    "channel_est",
    "phase_tracker",
    "frame_decoder",
)


def parse_proc_stat(text: str) -> dict[str, tuple[int, int]]:
    """Each CPU's busy and total time, in ticks, from /proc/stat.

    Idle time is `idle` and `iowait`; the rest of the first 8 fields are
    busy. Guest time is already in user time, so it isn't counted again.
    """
    cpus = {}
    for line in text.splitlines():
        name, *fields = line.split()
        if not name.startswith("cpu") or name == "cpu":
            continue
        ticks = [int(field) for field in fields[:8]]
        idle = ticks[3] + ticks[4]
        cpus[name] = (sum(ticks) - idle, sum(ticks))
    return cpus


def busy_fractions(
    before: dict[str, tuple[int, int]], after: dict[str, tuple[int, int]]
) -> dict[str, float]:
    """Each CPU's busy fraction between two readings of /proc/stat."""
    fractions = {}
    for cpu, (busy, total) in after.items():
        old_busy, old_total = before.get(cpu, (busy, total))
        elapsed = total - old_total
        fractions[cpu] = (busy - old_busy) / elapsed if elapsed else 0.0
    return fractions


class CpuSampler:
    """Samples each CPU's busy fraction every CPU_SAMPLE_S, on a thread."""

    def __init__(self, read: Callable[[], str] | None = None) -> None:
        """Starts with no samples; read() returns /proc/stat's text."""
        self.read = read or (lambda: pathlib.Path("/proc/stat").read_text())
        self.samples: list[dict[str, float]] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def __enter__(self) -> Self:
        """Starts sampling."""
        self._thread.start()
        return self

    def __exit__(self, *_: object) -> None:
        """Stops sampling."""
        self._stop.set()
        self._thread.join()

    def _run(self) -> None:
        try:
            last = parse_proc_stat(self.read())
            while not self._stop.wait(CPU_SAMPLE_S):
                now = parse_proc_stat(self.read())
                self.samples.append(busy_fractions(last, now))
                last = now
        except OSError, ValueError, IndexError:
            log.exception("Stopped sampling the CPUs")


def cpu_summary(samples: Sequence[dict[str, float]]) -> dict[str, float]:
    """Each CPU's use in a step: the 90th percentile of its samples, in %.

    A step's first seconds open the USRP, when the receiver isn't running
    yet, so the mean would understate its load.
    """
    if not samples:
        return {}
    summary = {}
    for cpu in samples[0]:
        values = sorted(sample.get(cpu, 0.0) for sample in samples)
        summary[cpu] = (
            100 * values[min(len(values) - 1, int(0.9 * len(values)))]
        )
    return summary


@dataclasses.dataclass(frozen=True)
class Step:
    """One realtime step: a workload at a bandwidth, once."""

    workload: str
    bandwidth_mhz: float
    repetition: int

    @property
    def name(self) -> str:
        """Like "qam64-3/4 at 20 MHz #2", for the second repetition."""
        return (
            f"{self.workload} at {self.bandwidth_mhz:g} MHz #{self.repetition}"
        )


def step_args(
    step: Step,
    usrp: hardware.Usrp,
    radio: dict[str, Any],
    seconds: float,
) -> tuple[list[str], float]:
    """fun-ofdm's arguments for a step, and its time limit.

    A step with frames sends seconds' worth of them over loopback. An idle
    step runs rx, with nothing sent, and is stopped IDLE_START_S plus
    seconds after it starts. rx has no port options, so it receives on its
    default port, which only hears noise too.
    """
    bandwidth = step.bandwidth_mhz
    if step.workload == IDLE:
        args = [
            "rx",
            f"--device=serial={usrp.serial}",
            *hardware.tuning_args(radio),
            f"--bandwidth={bandwidth}",
            f"--gain={radio['rx_gain']}",
        ]
        return args, IDLE_START_S + seconds
    frame_s = hardware.frame_seconds(step.workload, bandwidth)
    packets = max(1, round(seconds / (frame_s + FRAME_GAP_S)))
    args = hardware.loopback_args(
        usrp, {**radio, "bandwidth_mhz": bandwidth}, step.workload, packets
    )
    return args, RUN_START_S + packets * (2 * frame_s + FRAME_LIMIT_GAP_S)


def histogram_us(histogram: dict[str, Any]) -> dict[str, float]:
    """A --json histogram's p50 and p99, from ns to µs."""
    return {key: histogram[key] / 1000 for key in ("p50", "p99")}


def step_record(
    step: Step, run: hardware.Run, cpu: dict[str, float]
) -> dict[str, Any]:
    """What result.json records of a step: how it ran, and what it measured.

    A step with frames is complete if it finished. An idle step runs until
    it's stopped at its time limit, so it's complete if it was stopped, and
    received samples.
    """
    summary = run.summary
    record: dict[str, Any] = {
        **dataclasses.asdict(step),
        "status": run.status,
        "seconds": run.seconds,
        "complete": False,
        "cpu_percent": cpu,
    }
    if summary is None or "metrics" not in summary:
        return record
    metrics = summary["metrics"]
    if step.workload == IDLE:
        # A build without metrics (--config=no_metrics) has no histograms
        received = (
            not metrics.get("enabled", True)
            or metrics.get("arrival_interval", {}).get("count", 0) > 0
        )
        record["complete"] = run.status == hardware.TIMED_OUT and received
    else:
        record["complete"] = run.status == hardware.FINISHED
    record["overflows"] = metrics.get("overflows")
    if step.workload != IDLE:
        sent = summary["sent"]["packets"]
        received = summary["received"]["packets"]
        record["sent"] = sent
        # A stopped run has no check of the data, and a duplicate counts in
        # neither
        record["lost"] = summary.get("lost", max(0, sent - received))
        record["latency_us"] = histogram_us(metrics["latency"])
    blocks = metrics.get("blocks", {})
    record["processing_us"] = {
        block: histogram_us(blocks[block]["processing"])
        for block in BLOCKS
        if block in blocks
    }
    return record


def keeps_up(records: Sequence[dict[str, Any]]) -> bool:
    """Whether every repetition of a step completed with no overflows."""
    return bool(records) and all(
        r["complete"] and r.get("overflows") == 0 for r in records
    )


def highest_kept_up(
    records: Sequence[dict[str, Any]],
) -> dict[str, float | None]:
    """Each workload's highest bandwidth it kept up with, and every lower one.

    It's None if it didn't keep up with the lowest. Keeping up at a higher
    bandwidth after failing at a lower one is luck, not a higher limit.
    """
    highest: dict[str, float | None] = {}
    for record in records:
        highest.setdefault(record["workload"], None)
    for workload in highest:
        for bandwidth in sorted({r["bandwidth_mhz"] for r in records}):
            steps = [
                r
                for r in records
                if r["workload"] == workload and r["bandwidth_mhz"] == bandwidth
            ]
            if not keeps_up(steps):
                break
            highest[workload] = bandwidth
    return highest


def median_of(values: Sequence[float | None]) -> float | None:
    """The median of the values that were measured, or None."""
    found = [v for v in values if v is not None]
    return statistics.median(found) if found else None


def realtime_table(records: Sequence[dict[str, Any]]) -> str:
    """result.md's tables: the headline, then each step's medians."""
    highest = highest_kept_up(records)
    headline = [
        {
            "Workload": workload,
            "Highest bandwidth with no overflows": "none"
            if bandwidth is None
            else f"{bandwidth:g} MHz",
        }
        for workload, bandwidth in highest.items()
    ]
    rows = []
    for workload in highest:
        for bandwidth in sorted({r["bandwidth_mhz"] for r in records}):
            steps = [
                r
                for r in records
                if r["workload"] == workload and r["bandwidth_mhz"] == bandwidth
            ]
            if not steps:
                continue

            def text(value: float | None, digits: int = 0) -> str:
                return "?" if value is None else f"{value:.{digits}f}"

            busiest = median_of(
                [max(r["cpu_percent"].values(), default=None) for r in steps]
            )
            rows.append(
                {
                    "Workload": workload,
                    "MHz": f"{bandwidth:g}",
                    "Runs": f"{sum(r['complete'] for r in steps)}/{len(steps)}",
                    "Overflows": text(
                        max(
                            (
                                r["overflows"]
                                for r in steps
                                if r.get("overflows") is not None
                            ),
                            default=None,
                        )
                    ),
                    "Lost": "—"
                    if workload == IDLE
                    else text(median_of([r.get("lost") for r in steps])),
                    "Latency p99 (µs)": "—"
                    if workload == IDLE
                    else text(
                        median_of(
                            [r.get("latency_us", {}).get("p99") for r in steps]
                        )
                    ),
                    "Busiest CPU (%)": text(busiest),
                    "Kept up": "yes" if keeps_up(steps) else "no",
                }
            )
    return (
        result.markdown_table(headline)
        + "\nEach row is the median of its runs, but Overflows, the most in any"
        " of them. A CPU's use is the 90th percentile of its samples every"
        f" {CPU_SAMPLE_S:g} s.\n\n" + result.markdown_table(rows)
    )


#: Runs one step: the step, fun-ofdm's arguments and the time limit → the
#: run and each CPU's use.
StepRunner = Callable[
    [Step, Sequence[str], float], tuple[hardware.Run, dict[str, float]]
]


def realtime(
    run_step: StepRunner,
    usrp: hardware.Usrp,
    radio: dict[str, Any],
    workloads: Sequence[str],
    bandwidths: Sequence[float],
    repetitions: int,
    seconds: float,
    on_step: Callable[[list[dict[str, Any]]], None] | None = None,
) -> list[dict[str, Any]]:
    """Runs every realtime step, repetition by repetition.

    Each repetition goes through every bandwidth and workload before the
    next starts, so a slow minute doesn't land on one step's runs.

    on_step, if given, gets the records so far after each step, to save
    them, so an interrupted sweep keeps what it measured.

    Returns:
        Each step's record (step_record()).
    """
    records = []
    for repetition in range(1, repetitions + 1):
        for bandwidth in bandwidths:
            for workload in workloads:
                step = Step(workload, bandwidth, repetition)
                args, limit_s = step_args(step, usrp, radio, seconds)
                log.info("%s", step.name)
                run, cpu = run_step(step, args, limit_s)
                records.append(step_record(step, run, cpu))
                if on_step:
                    on_step(records)
    return records


def hardware_runner(
    commands: hardware.Commands,
    fun_ofdm: pathlib.Path,
    logs: pathlib.Path | None,
) -> StepRunner:
    """Runs steps on the B210, sampling the CPUs, and keeps failed runs' logs."""

    def run_step(
        step: Step, args: Sequence[str], limit_s: float
    ) -> tuple[hardware.Run, dict[str, float]]:
        idle = step.workload == IDLE
        with CpuSampler() as sampler:
            run = hardware.run_fun_ofdm(
                commands,
                fun_ofdm,
                step.name,
                args,
                limit_s,
                read_threads=not idle,
            )
        if logs and not keeps_up([step_record(step, run, {})]):
            logs.mkdir(parents=True, exist_ok=True)
            (
                logs / (step.name.replace("/", "-").replace(" ", "_") + ".log")
            ).write_text(hardware.run_log(run))
        return run, cpu_summary(sampler.samples)

    return run_step


def add_radio_options(parser: argparse.ArgumentParser) -> None:
    """The options every sweep shares: the binary, the radio and the ports."""
    parser.add_argument(
        "--fun-ofdm", type=pathlib.Path, default=hardware.DEFAULT_FUN_OFDM
    )
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        metavar="DIR",
        help="write result.json and result.md to DIR",
    )
    tuning = parser.add_mutually_exclusive_group()
    tuning.add_argument(
        "--channel", type=int, help=f"(default: {hardware.DEFAULT_CHANNEL})"
    )
    tuning.add_argument(
        "--freq", type=float, help="in GHz, instead of --channel"
    )
    parser.add_argument("--tx-gain", type=float, default=80)
    parser.add_argument("--rx-gain", type=float, default=60)
    parser.add_argument(
        "--tx-port",
        choices=list(hardware.PORTS),
        default=hardware.DEFAULT_TX_PORT,
    )
    parser.add_argument(
        "--rx-port",
        choices=list(hardware.PORTS),
        default=hardware.DEFAULT_RX_PORT,
    )


def radio_settings(args: argparse.Namespace) -> dict[str, Any]:
    """The radio settings from the options, as the hardware tests have them.

    There's no bandwidth: each step sets its own.
    """
    tuning = (
        {"freq_ghz": args.freq}
        if args.freq is not None
        else {"channel": args.channel or hardware.DEFAULT_CHANNEL}
    )
    return {
        **tuning,
        "tx_gain": args.tx_gain,
        "rx_gain": args.rx_gain,
        "tx_port": args.tx_port,
        "rx_port": args.rx_port,
    }


def main(argv: Sequence[str] | None = None) -> int:
    """Runs a sweep from the command line."""
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    sweeps = parser.add_subparsers(dest="sweep", required=True)
    realtime_parser = sweeps.add_parser(
        "realtime",
        help="the highest sample rate the receiver keeps up with",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    add_radio_options(realtime_parser)
    realtime_parser.add_argument(
        "--bandwidths", type=float, nargs="+", default=list(BANDWIDTHS_MHZ)
    )
    realtime_parser.add_argument(
        "--workloads", nargs="+", choices=WORKLOADS, default=list(WORKLOADS)
    )
    realtime_parser.add_argument("--repetitions", type=int, default=REPETITIONS)
    realtime_parser.add_argument(
        "--seconds",
        type=float,
        default=STEP_SECONDS,
        help="how long each step sends frames or receives noise",
    )
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    if not args.fun_ofdm.exists():
        parser.error(f"{args.fun_ofdm} doesn't exist: bazel run install")
    if args.out and args.out.exists() and not args.out.is_dir():
        parser.error(f"--out {args.out} isn't a directory: it takes one")
    problem = hardware.port_problem(
        hardware.PORTS[args.tx_port], hardware.PORTS[args.rx_port]
    )
    if problem:
        parser.error(problem)
    if args.repetitions < 1 or args.seconds <= 0:
        parser.error("--repetitions and --seconds must be positive")

    commands = hardware.Commands()
    try:
        usrp = hardware.find_usrp(commands)
    except hardware.HardwareError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    radio = radio_settings(args)
    # Before the sweep, which it describes
    prov = result.provenance(
        f"tools/hardware/hardware_sweep.py {args.sweep}",
        {
            "bandwidths": args.bandwidths,
            "workloads": args.workloads,
            "repetitions": args.repetitions,
            "seconds": args.seconds,
        },
        {"fun-ofdm": args.fun_ofdm},
        hardware.provenance(usrp, radio),
        sources=[pathlib.Path(__file__), pathlib.Path(hardware.__file__)],
    )

    def save(records: list[dict[str, Any]], partial: bool = True) -> None:
        if args.out:
            result.write(
                args.out,
                prov,
                {
                    "complete": not partial,
                    "highest_kept_up_mhz": highest_kept_up(records),
                    "steps": records,
                },
                "Real time on hardware" + (" (partial)" if partial else ""),
                realtime_table(records),
            )

    records = realtime(
        hardware_runner(
            commands, args.fun_ofdm, args.out / "logs" if args.out else None
        ),
        usrp,
        radio,
        args.workloads,
        args.bandwidths,
        args.repetitions,
        args.seconds,
        save,
    )
    save(records, partial=False)
    print(realtime_table(records), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
