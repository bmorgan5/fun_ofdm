"""Measures packet error rate (PER) against SNR, for every rate.

Each point runs `fun-ofdm sim` with 1000-byte payloads through an AWGN channel
at one SNR, and counts the frames lost. Every point uses the same seed, so
every point has the same data and the same noise, only scaled: a higher SNR is
the same run with quieter noise, which keeps each curve close to monotonic.

    uv run tools/per/sweep.py --out per.json

It prints the SNR at which each rate's PER falls to 10%, and with --out writes
every point, with how the receiver lost each frame (see README.md).
"""

import argparse
import concurrent.futures
import dataclasses
import itertools
import json
import math
import os
import pathlib
import platform
import subprocess
import sys
import threading
from collections.abc import Callable, Iterable, Sequence
from typing import Any

REPO = pathlib.Path(__file__).resolve().parents[2]

#: The fun-ofdm binary for this machine's architecture, which
#: `bazel run install` copies there.
DEFAULT_FUN_OFDM = (
    REPO / "out" / f"linux_{platform.machine()}" / "bin" / "fun-ofdm"
)

#: Every --phy-rate, from the slowest to the fastest.
RATES = (
    "bpsk-1/2",
    "bpsk-2/3",
    "bpsk-3/4",
    "qpsk-1/2",
    "qpsk-2/3",
    "qpsk-3/4",
    "qam16-1/2",
    "qam16-2/3",
    "qam16-3/4",
    "qam64-2/3",
    "qam64-3/4",
)

#: The PER at which the sweep reports each rate's SNR.
TARGET_PER = 0.1

#: The payload size the curves are measured at, with its sequence number.
PAYLOAD_SIZE = 1000

#: z for a two-sided 95% confidence interval.
Z_95 = 1.959963984540054


@dataclasses.dataclass(frozen=True)
class Point:
    """One SNR's result for one rate."""

    snr_db: float
    sent: int
    lost: int
    #: The receiver's counters (`metrics.frames` in sim's JSON).
    detected: int
    sync_lost: int
    signal_failed: int
    crc_failed: int

    @property
    def per(self) -> float:
        """The packet error rate: the fraction of frames lost."""
        return self.lost / self.sent

    def interval(self, z: float = Z_95) -> tuple[float, float]:
        """The PER's confidence interval, by default 95%."""
        return wilson_interval(self.lost, self.sent, z)


def wilson_interval(
    failures: int, trials: int, z: float = Z_95
) -> tuple[float, float]:
    """Wilson's score interval for a binomial proportion.

    Unlike the normal approximation, it stays inside [0, 1] and is still
    useful with no failures, or no successes.

    Args:
        failures: The number of failures.
        trials: The number of trials, more than 0.
        z: The normal quantile of the confidence, e.g. 1.96 for 95%.

    Returns:
        The interval's lower and upper bounds.
    """
    p = failures / trials
    denominator = 1 + z * z / trials
    centre = (p + z * z / (2 * trials)) / denominator
    half = (
        z
        * math.sqrt(p * (1 - p) / trials + z * z / (4 * trials * trials))
        / denominator
    )
    return max(0.0, centre - half), min(1.0, centre + half)


def snr_at_per(points: Sequence[Point], target: float) -> float | None:
    """The SNR at which a curve first falls to a PER.

    Interpolates log10(PER) linearly between the two points around the
    crossing, so a curve's steep part doesn't pull the answer towards either
    point. A point with no losses counts as a tenth of a frame lost.

    Args:
        points: The curve, in increasing SNR.
        target: The PER, between 0 and 1.

    Returns:
        The SNR in dB, or None if the curve never falls to the PER, or starts
        below it.
    """
    for low, high in itertools.pairwise(points):
        if low.per > target >= high.per:

            def log_per(point: Point) -> float:
                return math.log10(max(point.per, 0.1 / point.sent))

            fraction = (math.log10(target) - log_per(low)) / (
                log_per(high) - log_per(low)
            )
            return low.snr_db + fraction * (high.snr_db - low.snr_db)
    return None


def is_monotonic(points: Sequence[Point]) -> bool:
    """Whether a curve's PER never rises with SNR, beyond chance.

    A rise counts only if the two points' 95% intervals don't overlap.
    """
    for low, high in itertools.pairwise(points):
        if high.interval()[0] > low.interval()[1]:
            return False
    return True


#: Runs one point: (rate, SNR in dB, frames) → the point.
Runner = Callable[[str, float, int], Point]


def sim_runner(fun_ofdm: pathlib.Path, seed: int) -> Runner:
    """A runner that runs a point with `fun-ofdm sim`.

    Args:
        fun_ofdm: The binary.
        seed: The seed of every point.

    Returns:
        The runner. It raises subprocess.CalledProcessError if sim fails for
        any reason but losing frames.
    """

    def run(rate: str, snr_db: float, frames: int) -> Point:
        command = [
            str(fun_ofdm),
            "sim",
            "--phy-rate",
            rate,
            "--snr",
            f"{snr_db:g}",
            "--packets",
            str(frames),
            "--payload-size",
            str(PAYLOAD_SIZE),
            "--seed",
            str(seed),
            "--json",
        ]
        # sim exits with 1 when it loses frames, which is the point here.
        done = subprocess.run(
            command, check=False, capture_output=True, text=True
        )
        if done.returncode not in (0, 1):
            error = subprocess.CalledProcessError(
                done.returncode, command, done.stdout, done.stderr
            )
            # So the traceback says why, like a missing shared library.
            error.add_note(f"stderr:\n{done.stderr}")
            raise error
        summary = json.loads(done.stdout)
        counters = summary["metrics"]["frames"]
        return Point(
            snr_db=snr_db,
            sent=frames,
            lost=summary["lost"],
            detected=counters["detected"],
            sync_lost=counters["sync_lost"],
            signal_failed=counters["signal_failed"],
            crc_failed=counters["crc_failed"][rate],
        )

    return run


def snr_grid(start: float, stop: float, step: float) -> list[float]:
    """The SNRs from start to stop, inclusive, every step dB."""
    count = math.floor((stop - start) / step + 1e-9) + 1
    return [round(start + i * step, 6) for i in range(count)]


def sweep(
    run: Runner,
    rates: Iterable[str],
    snrs: Sequence[float],
    frames: int,
    jobs: int,
    on_point: Callable[[str, Point], None] | None = None,
) -> dict[str, list[Point]]:
    """Runs every rate at every SNR.

    Args:
        run: Runs one point.
        rates: The --phy-rate names.
        snrs: The SNRs in dB, in increasing order.
        frames: The frames to send at each point.
        jobs: How many points to run at once.
        on_point: Called with each point's rate and result as it finishes,
            from the thread that ran it.

    Returns:
        Each rate's curve, in increasing SNR.
    """
    rates = list(rates)

    def run_point(rate: str, snr: float) -> Point:
        point = run(rate, snr, frames)
        if on_point:
            on_point(rate, point)
        return point

    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        futures = {
            (rate, snr): pool.submit(run_point, rate, snr)
            for rate in rates
            for snr in snrs
        }
        return {
            rate: [futures[(rate, snr)].result() for snr in snrs]
            for rate in rates
        }


def progress_printer(total: int) -> Callable[[str, Point], None]:
    """Prints each point to stderr as it finishes, with how many are done.

    On a terminal, each point overwrites the last; otherwise, as in CI's
    logs, each gets its own line.
    """
    done = 0
    lock = threading.Lock()
    tty = sys.stderr.isatty()
    # On a terminal: back to the line's start, and clear what's left of the
    # last point's line.
    end, clear = ("\r", "\033[K") if tty else ("\n", "")

    def on_point(rate: str, point: Point) -> None:
        nonlocal done
        with lock:
            done += 1
            print(
                f"[{done}/{total}] {rate} at {point.snr_db:g} dB: "
                f"{point.lost}/{point.sent} lost{clear}",
                end=end if done < total else "\n",
                file=sys.stderr,
                flush=True,
            )

    return on_point


def to_json(
    curves: dict[str, list[Point]], frames: int, seed: int
) -> dict[str, Any]:
    """The sweep's results, as JSON."""

    def point_json(point: Point) -> dict[str, Any]:
        low, high = point.interval()
        return {
            **dataclasses.asdict(point),
            "per": point.per,
            "per_low": low,
            "per_high": high,
        }

    return {
        "payload_size": PAYLOAD_SIZE,
        "frames": frames,
        "seed": seed,
        "target_per": TARGET_PER,
        "rates": {
            rate: {
                "snr_at_target_per": snr_at_per(points, TARGET_PER),
                "monotonic": is_monotonic(points),
                "points": [point_json(p) for p in points],
            }
            for rate, points in curves.items()
        },
    }


def table(curves: dict[str, list[Point]]) -> str:
    """A Markdown table of each rate's SNR at 10% PER."""
    lines = [
        "| Rate | SNR at 10% PER | Monotonic |",
        "|------|----------------|-----------|",
    ]
    for rate, points in curves.items():
        snr = snr_at_per(points, TARGET_PER)
        text = "not reached" if snr is None else f"{snr:.1f} dB"
        lines.append(
            f"| {rate} | {text} | {'yes' if is_monotonic(points) else 'no'} |"
        )
    return "\n".join(lines)


def main(argv: Sequence[str] | None = None) -> int:
    """Runs the sweep from the command line."""
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--fun-ofdm", type=pathlib.Path, default=DEFAULT_FUN_OFDM
    )
    parser.add_argument(
        "--rates", nargs="+", choices=RATES, default=list(RATES)
    )
    parser.add_argument("--snr-min", type=float, default=0.0)
    parser.add_argument("--snr-max", type=float, default=30.0)
    parser.add_argument("--snr-step", type=float, default=1.0)
    parser.add_argument(
        "--frames", type=int, default=500, help="frames at each point"
    )
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, (os.cpu_count() or 2) // 2),
        help="points to run at once",
    )
    parser.add_argument(
        "--out", type=pathlib.Path, help="write every point as JSON"
    )
    args = parser.parse_args(argv)

    if not args.fun_ofdm.exists():
        parser.error(f"{args.fun_ofdm} doesn't exist: bazel run install")
    if args.snr_step <= 0 or args.snr_max < args.snr_min:
        parser.error("the SNRs need --snr-step > 0 and --snr-max >= --snr-min")
    if args.frames < 1:
        parser.error("--frames must be at least 1")

    snrs = snr_grid(args.snr_min, args.snr_max, args.snr_step)
    total = len(args.rates) * len(snrs)
    print(
        f"Running {total} points (rates x SNRs) of {args.frames} frames, "
        f"{args.jobs} at a time, with {args.fun_ofdm}",
        file=sys.stderr,
    )
    curves = sweep(
        sim_runner(args.fun_ofdm, args.seed),
        args.rates,
        snrs,
        args.frames,
        args.jobs,
        progress_printer(total),
    )
    print(table(curves))
    if args.out:
        args.out.write_text(
            json.dumps(to_json(curves, args.frames, args.seed), indent=2) + "\n"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
