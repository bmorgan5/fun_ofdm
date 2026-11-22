"""Sweeps on hardware, over the B210 loopback in hil/README.md.

    uv run tools/hardware/hardware_sweep.py realtime --out realtime/
    uv run tools/hardware/hardware_sweep.py per --sim per/ --out per-hardware/

- `realtime`: the highest sample rate the receiver keeps up with. At each of
  802.11a's bandwidths, 5, 10 and 20 MHz, which are also the sample rates, it
  sends frames at BPSK 1/2, the lightest rate to decode, and at QAM64 3/4, the
  heaviest, and receives nothing but noise (`idle`), the receiver's floor,
  several times each. Each step records the USRP's overflows, the frames lost,
  the latency, each block's processing time, and the CPU use of each core,
  from /proc/stat.
- `per`: packet error rate against SNR at every rate, against the simulated
  curves from tools/per/sweep.py. It steps the TX gain 1 dB at a time across
  each rate's waterfall, and records the PER and the receiver's SNR estimate.
  SNR follows TX gain one for one, so it fits SNR to TX gain where the
  estimate is reliable, and uses the fit at every step.

With --out DIR, each writes its results as tools/results/result.py says.
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
sys.path.insert(0, str(REPO / "tools" / "per"))

import hardware
import result
import sweep as per_sweep

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


#: The bandwidth per runs at, in MHz: one the receiver keeps up with.
PER_BANDWIDTH_MHZ = 5.0

#: Frames at each per step, with the simulated curves' payload size.
PER_FRAMES = 500
PER_PAYLOAD_SIZE = per_sweep.PAYLOAD_SIZE

#: The TX gain of the calibration step, which finds roughly where each
#: rate's waterfall is, and its frames.
CALIBRATION_GAIN = 75.0
CALIBRATION_FRAMES = 100

#: Each rate starts this many dB either side of its simulated 10% PER point.
PER_HALF_WIDTH_DB = 5

#: It then adds steps, up to this many at each end, until the PER is at
#: least PER_TOP at the bottom, and at most PER_BOTTOM at the top.
PER_MAX_EXTRA = 6
PER_TOP = 0.9
PER_BOTTOM = 0.01

#: The B210's TX gain range, in dB.
TX_GAIN_RANGE = (0.0, 89.0)

#: A step's SNR estimate is reliable enough to fit if at least this
#: fraction of its frames reached it, and its PER is below FIT_MAX_PER: at
#: lower SNRs, only the frames that happened to be louder get through, so
#: the estimate reads high.
FIT_MIN_ESTIMATED = 0.5
FIT_MAX_PER = 0.5

#: A fit whose slope is further than this from 1 is flagged in result.md:
#: SNR should follow TX gain one for one.
FIT_MAX_SLOPE_ERROR = 0.1

#: The SNR a rate the simulated sweep doesn't have is centred on, if the
#: sweep has no rate at all: about where every rate up to 16-QAM is.
FALLBACK_SIM_SNR_DB = 18.0


@dataclasses.dataclass(frozen=True)
class PerStep:
    """One per step: a rate at a TX gain, and what it measured."""

    rate: str
    tx_gain: float
    status: str
    sent: int
    lost: int
    #: The receiver's median SNR estimate for the rate, and how many frames
    #: it's from (metrics.snr in the --json summary), or None for none.
    snr_estimate: float | None
    estimates: int
    #: How the receiver lost frames (metrics.frames).
    detected: int
    sync_lost: int
    signal_failed: int
    crc_failed: int

    @property
    def per(self) -> float:
        """The packet error rate."""
        return self.lost / self.sent if self.sent else 1.0

    @property
    def finished(self) -> bool:
        """Whether its run finished, so its PER counts: not one that failed."""
        return self.status == hardware.FINISHED


def per_step(
    rate: str, tx_gain: float, frames: int, run: hardware.Run
) -> PerStep:
    """A per step's result, from its run of frames.

    A run without a summary lost every frame, and counts as failed.
    """
    summary = run.summary
    if summary is None:
        return PerStep(
            rate,
            tx_gain,
            run.status,
            frames,
            frames,
            None,
            0,
            0,
            0,
            0,
            0,
        )
    sent = summary["sent"]["packets"]
    received = summary["received"]["packets"]
    metrics = summary["metrics"]
    snr = metrics.get("snr", {}).get(rate, {})
    counters = metrics["frames"]
    return PerStep(
        rate=rate,
        tx_gain=tx_gain,
        status=run.status,
        sent=sent,
        lost=summary.get("lost", sent - received),
        snr_estimate=snr.get("p50"),
        estimates=snr.get("count", 0),
        detected=counters["detected"],
        sync_lost=counters["sync_lost"],
        signal_failed=counters["signal_failed"],
        crc_failed=counters["crc_failed"][rate],
    )


@dataclasses.dataclass(frozen=True)
class Fit:
    """SNR against TX gain: SNR = TX gain + offset_db."""

    offset_db: float
    #: The least-squares slope, which should be about 1, or None with fewer
    #: than two points.
    slope: float | None
    #: The TX gains of the steps it was fitted to.
    gains: tuple[float, ...]


def fit_snr(steps: Sequence[PerStep]) -> Fit | None:
    """Fits SNR = TX gain + offset to the steps with reliable estimates.

    The offset is the median of their estimates minus their gains, which a
    single bad step barely moves. The slope is reported, as a check on the
    assumption that SNR follows TX gain one for one.

    Returns:
        The fit, or None if no step's estimate is reliable.
    """
    reliable = [
        s
        for s in steps
        if s.snr_estimate is not None
        and s.sent
        and s.finished
        and s.per < FIT_MAX_PER
        and s.estimates >= FIT_MIN_ESTIMATED * s.sent
    ]
    if not reliable:
        return None
    offset = statistics.median(
        s.snr_estimate - s.tx_gain
        for s in reliable
        if s.snr_estimate is not None
    )
    slope = None
    if len({s.tx_gain for s in reliable}) > 1:
        slope = statistics.linear_regression(
            [s.tx_gain for s in reliable],
            [s.snr_estimate for s in reliable if s.snr_estimate is not None],
        ).slope
    return Fit(offset, slope, tuple(s.tx_gain for s in reliable))


def points(steps: Sequence[PerStep], fit: Fit) -> list[Any]:
    """The steps as tools/per/sweep.py's points, at the fitted SNRs.

    A step whose run failed is left out: its PER says nothing about the SNR.
    """
    return [
        per_sweep.Point(
            snr_db=s.tx_gain + fit.offset_db,
            sent=s.sent,
            lost=s.lost,
            detected=s.detected,
            sync_lost=s.sync_lost,
            signal_failed=s.signal_failed,
            crc_failed=s.crc_failed,
        )
        for s in sorted(steps, key=lambda s: s.tx_gain)
        if s.finished and s.sent
    ]


#: Runs one per step: the rate, the TX gain and the frames → the step.
PerRunner = Callable[[str, float, int], PerStep]


def clamp_gain(gain: float) -> float:
    """A TX gain within the B210's range."""
    return min(max(gain, TX_GAIN_RANGE[0]), TX_GAIN_RANGE[1])


def per_rate(
    run_step: PerRunner,
    rate: str,
    center: float,
    on_step: Callable[[list[PerStep]], None] | None = None,
) -> list[PerStep]:
    """One rate's steps, across its waterfall, in increasing TX gain.

    It extends the range by the steps at its ends that finished: a failed
    step's PER says nothing.

    Args:
        run_step: Runs a step.
        rate: The --phy-rate.
        center: The TX gain where its PER should be about 10%.
        on_step: Gets the steps so far after each one, to save them.
    """
    low, high = PER_HALF_WIDTH_DB, PER_HALF_WIDTH_DB
    gains = sorted(
        {clamp_gain(round(center) + d) for d in range(-low, high + 1)}
    )
    steps: dict[float, PerStep] = {}

    def run(gain: float) -> None:
        steps[gain] = run_step(rate, gain, PER_FRAMES)
        if on_step:
            on_step([steps[g] for g in sorted(steps)])

    for gain in gains:
        run(gain)
    if not any(step.finished for step in steps.values()):
        # Like the USRP gone: more steps would fail too, each at its limit
        return [steps[g] for g in sorted(steps)]
    for _ in range(PER_MAX_EXTRA):
        lowest = min(steps)
        finished = [steps[g] for g in sorted(steps) if steps[g].finished]
        if lowest <= TX_GAIN_RANGE[0] or (
            finished and finished[0].per >= PER_TOP
        ):
            break
        run(lowest - 1)
    for _ in range(PER_MAX_EXTRA):
        highest = max(steps)
        finished = [steps[g] for g in sorted(steps) if steps[g].finished]
        if highest >= TX_GAIN_RANGE[1] or (
            finished and finished[-1].per <= PER_BOTTOM
        ):
            break
        run(highest + 1)
    return [steps[g] for g in sorted(steps)]


def simulated_snrs(sim: dict[str, Any]) -> dict[str, float | None]:
    """Each rate's SNR at 10% PER from a tools/per/sweep.py result.json."""
    return {
        rate: curve["snr_at_target_per"] for rate, curve in sim["rates"].items()
    }


def per(
    run_step: PerRunner,
    rates: Sequence[str],
    sim_snrs: dict[str, float | None],
    on_rate: Callable[[dict[str, Any]], None] | None = None,
) -> dict[str, Any]:
    """Runs every rate's steps, and works out its curve.

    A calibration step at CALIBRATION_GAIN finds the offset from TX gain to
    SNR, roughly, so each rate starts around its simulated 10% PER point.
    on_rate, if given, gets the results so far after each step, to save
    them: the rate in progress has its steps so far, and no fit yet.

    Returns:
        The calibration step, and for each rate: its steps, the fit, its SNR
        at 10% PER, the simulation's, and the difference, its implementation
        loss.
    """
    calibration = run_step(rates[0], CALIBRATION_GAIN, CALIBRATION_FRAMES)
    if fit_snr([calibration]) is None or calibration.snr_estimate is None:
        raise hardware.HardwareError(
            f"At TX gain {CALIBRATION_GAIN:g}, too few frames reached the "
            f"receiver ({calibration.lost} of {calibration.sent} lost, "
            f"{calibration.estimates} SNR estimates) to find the SNR: check "
            "the cable and the gains (hil/README.md)"
        )
    curves: dict[str, Any] = {}

    def save(
        rate: str | None = None, steps: list[PerStep] | None = None
    ) -> None:
        if on_rate is None:
            return
        found = dict(curves)
        if rate is not None:
            found[rate] = curve(steps or [], None, None, sim_snrs.get(rate))
        on_rate({"calibration": calibration, "rates": found})

    save()
    offset = calibration.snr_estimate - CALIBRATION_GAIN
    log.info(
        "TX gain %g: SNR %.1f dB", CALIBRATION_GAIN, calibration.snr_estimate
    )
    for rate in rates:
        sim_snr = sim_snrs.get(rate)
        target = (
            sim_snr
            if sim_snr is not None
            else max(
                (v for v in sim_snrs.values() if v is not None),
                default=FALLBACK_SIM_SNR_DB,
            )
        )
        steps = per_rate(
            run_step,
            rate,
            target - offset,
            lambda so_far, rate=rate: save(rate, so_far),
        )
        fit = fit_snr(steps)
        snr = None
        if fit is not None:
            snr = per_sweep.snr_at_per(points(steps, fit), per_sweep.TARGET_PER)
        curves[rate] = curve(steps, fit, snr, sim_snr)
        save()
    return {"calibration": calibration, "rates": curves}


def curve(
    steps: list[PerStep],
    fit: Fit | None,
    snr: float | None,
    sim_snr: float | None,
) -> dict[str, Any]:
    """One rate's results, as per() returns them."""
    return {
        "steps": steps,
        "fit": fit,
        "snr_at_target_per": snr,
        "simulated_snr_at_target_per": sim_snr,
        "implementation_loss_db": None
        if snr is None or sim_snr is None
        else snr - sim_snr,
    }


def sim_problem(sim: dict[str, Any]) -> str | None:
    """Why a result can't be the simulated curves per compares with, or None.

    It must be a whole tools/per/sweep.py result, with the same payload size
    and target PER.
    """
    if not str(sim.get("provenance", {}).get("tool", "")).startswith(
        "tools/per/sweep.py"
    ):
        return "it isn't a tools/per/sweep.py result"
    if sim.get("payload_size") != PER_PAYLOAD_SIZE:
        return f"its payload size isn't {PER_PAYLOAD_SIZE} bytes"
    if sim.get("target_per") != per_sweep.TARGET_PER:
        return f"its target PER isn't {per_sweep.TARGET_PER}"
    return None


def per_json(found: dict[str, Any]) -> dict[str, Any]:
    """per()'s results as JSON, with each step's PER and fitted SNR."""

    def step_json(step: PerStep, fit: Fit | None) -> dict[str, Any]:
        low, high = per_sweep.wilson_interval(step.lost, step.sent or 1)
        return {
            **dataclasses.asdict(step),
            "per": step.per,
            "per_low": low,
            "per_high": high,
            "snr_db": None if fit is None else step.tx_gain + fit.offset_db,
        }

    return {
        "bandwidth_mhz": PER_BANDWIDTH_MHZ,
        "payload_size": PER_PAYLOAD_SIZE,
        "frames": PER_FRAMES,
        "target_per": per_sweep.TARGET_PER,
        "calibration": dataclasses.asdict(found["calibration"]),
        "rates": {
            rate: {
                "snr_at_target_per": curve["snr_at_target_per"],
                "simulated_snr_at_target_per": curve[
                    "simulated_snr_at_target_per"
                ],
                "implementation_loss_db": curve["implementation_loss_db"],
                "fit": None
                if curve["fit"] is None
                else dataclasses.asdict(curve["fit"]),
                "steps": [step_json(s, curve["fit"]) for s in curve["steps"]],
            }
            for rate, curve in found["rates"].items()
        },
    }


def per_table(found: dict[str, Any]) -> str:
    """result.md's table: each rate's SNR at 10% PER, and its loss."""

    def db(value: float | None, sign: bool = False) -> str:
        if value is None:
            return "not reached"
        return f"{value:+.1f} dB" if sign else f"{value:.1f} dB"

    rows = []
    for rate, curve in found["rates"].items():
        fit = curve["fit"]
        rows.append(
            {
                "Rate": rate,
                "SNR at 10% PER": db(curve["snr_at_target_per"]),
                "In simulation": db(curve["simulated_snr_at_target_per"]),
                "Implementation loss": db(
                    curve["implementation_loss_db"], True
                ),
                "TX gain to SNR": "no fit"
                if fit is None
                else f"{fit.offset_db:+.1f} dB, slope "
                + ("?" if fit.slope is None else f"{fit.slope:.2f}")
                + (
                    " (not 1: check the fit)"
                    if fit.slope is not None
                    and abs(fit.slope - 1) > FIT_MAX_SLOPE_ERROR
                    else ""
                ),
            }
        )
    return (
        f"At {PER_BANDWIDTH_MHZ:g} MHz, {PER_FRAMES} frames of"
        f" {PER_PAYLOAD_SIZE} bytes a step. Each step's SNR is its TX gain"
        " plus the rate's offset, fitted to its receiver's estimates.\n\n"
        + result.markdown_table(rows)
    )


def hardware_per_runner(
    commands: hardware.Commands,
    fun_ofdm: pathlib.Path,
    usrp: hardware.Usrp,
    radio: dict[str, Any],
) -> PerRunner:
    """Runs per steps on the B210, over loopback."""
    radio = {**radio, "bandwidth_mhz": PER_BANDWIDTH_MHZ}

    def run_step(rate: str, tx_gain: float, frames: int) -> PerStep:
        frame_s = hardware.frame_seconds(
            rate, PER_BANDWIDTH_MHZ, PER_PAYLOAD_SIZE
        )
        run = hardware.run_fun_ofdm(
            commands,
            fun_ofdm,
            f"{rate} at TX gain {tx_gain:g}",
            hardware.loopback_args(
                usrp,
                {**radio, "tx_gain": tx_gain},
                rate,
                frames,
                payload_size=PER_PAYLOAD_SIZE,
            ),
            RUN_START_S + 4 * frame_s * frames,
        )
        step = per_step(rate, tx_gain, frames, run)
        log.info(
            "%s at TX gain %g: %d of %d lost, SNR %s",
            rate,
            tx_gain,
            step.lost,
            step.sent,
            "?" if step.snr_estimate is None else f"{step.snr_estimate:.1f} dB",
        )
        return step

    return run_step


def add_radio_options(
    parser: argparse.ArgumentParser, tx_gain: bool = True
) -> None:
    """The options every sweep shares: the binary, the radio and the ports.

    per sets the TX gain itself, so it has no --tx-gain.
    """
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
    if tx_gain:
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

    realtime's have no bandwidth, since each step sets its own, and per's
    have its bandwidth and no TX gain, which it sweeps.
    """
    tuning = (
        {"freq_ghz": args.freq}
        if args.freq is not None
        else {"channel": args.channel or hardware.DEFAULT_CHANNEL}
    )
    settings = {
        **tuning,
        "rx_gain": args.rx_gain,
        "tx_port": args.tx_port,
        "rx_port": args.rx_port,
    }
    if args.sweep == "per":
        return {**settings, "bandwidth_mhz": PER_BANDWIDTH_MHZ}
    return {**settings, "tx_gain": args.tx_gain}


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
    per_parser = sweeps.add_parser(
        "per",
        help="PER vs SNR at every rate, against the simulated curves",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    add_radio_options(per_parser, tx_gain=False)
    per_parser.add_argument(
        "--sim",
        type=pathlib.Path,
        required=True,
        metavar="DIR",
        help="a tools/per/sweep.py --out DIR to compare with",
    )
    per_parser.add_argument(
        "--rates",
        nargs="+",
        choices=per_sweep.RATES,
        default=list(per_sweep.RATES),
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
    if args.sweep == "realtime" and (args.repetitions < 1 or args.seconds <= 0):
        parser.error("--repetitions and --seconds must be positive")
    sim = None
    if args.sweep == "per":
        try:
            sim = result.read(args.sim)
        except (OSError, ValueError) as error:
            parser.error(f"--sim {args.sim}: {error}")
        problem = sim_problem(sim)
        if problem:
            parser.error(f"--sim {args.sim}: {problem}")

    commands = hardware.Commands()
    try:
        usrp = hardware.find_usrp(commands)
    except hardware.HardwareError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    radio = radio_settings(args)
    if args.sweep == "realtime":
        options = {
            "bandwidths": args.bandwidths,
            "workloads": args.workloads,
            "repetitions": args.repetitions,
            "seconds": args.seconds,
        }
    else:
        options = {
            "rates": args.rates,
            "sim_sha256": result.sha256(args.sim / "result.json"),
        }
    # Before the sweep, which it describes
    prov = result.provenance(
        f"tools/hardware/hardware_sweep.py {args.sweep}",
        options,
        {"fun-ofdm": args.fun_ofdm},
        hardware.provenance(usrp, radio),
        sources=[
            pathlib.Path(__file__),
            pathlib.Path(hardware.__file__),
            REPO / "tools" / "per" / "sweep.py",
        ],
    )
    if args.sweep == "per":
        assert sim is not None

        def save_per(found: dict[str, Any], partial: bool = True) -> None:
            if args.out:
                result.write(
                    args.out,
                    prov,
                    {"complete": not partial, **per_json(found)},
                    "PER vs SNR on hardware"
                    + (" (partial)" if partial else ""),
                    per_table(found),
                )

        try:
            found = per(
                hardware_per_runner(commands, args.fun_ofdm, usrp, radio),
                args.rates,
                simulated_snrs(sim),
                save_per,
            )
        except hardware.HardwareError as error:
            print(f"error: {error}", file=sys.stderr)
            return 2
        save_per(found, partial=False)
        print(per_table(found), end="")
        return 0

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
