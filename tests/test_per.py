"""PER guardrails: how much SNR the receiver needs, against its baseline.

A change that makes the receiver faster but needs more SNR to decode is a
regression. These run fun-ofdm sim, with fixed seeds, so each gives the same
answer every time until the receiver changes. tools/per/sweep.py does the
measuring (see tools/per/README.md).
"""

import os
import pathlib

import pytest
import sweep

#: z for a one-sided 99% bound.
Z_99 = 2.3263478740408408

#: Frames at each fixed point: enough that the 99% bound is within about 1%
#: of the PER.
FIXED_FRAMES = 2000

#: The highest PER allowed at each fixed point.
MAX_PER = 0.05

#: Each rate's fixed point: where its PER was 1% to 2% in the baseline. About
#: 1.5 dB worse, the PER reaches 5%.
FIXED_SNR_DB = {
    "bpsk-1/2": 21.0,
    "bpsk-2/3": 21.0,
    "bpsk-3/4": 21.0,
    "qpsk-1/2": 21.0,
    "qpsk-2/3": 21.0,
    "qpsk-3/4": 21.0,
    "qam16-1/2": 21.0,
    "qam16-2/3": 21.0,
    "qam16-3/4": 21.0,
    "qam64-2/3": 22.0,
    "qam64-3/4": 22.0,
}

#: The baseline: each rate's SNR at 10% PER from the guardrail's sweep
#: (sweep.GUARDRAIL_SNRS). Update a value only with a release note that
#: explains why it got worse; when a change improves one, lower it.
BASELINE_SNR_DB = {
    "bpsk-1/2": 18.08,
    "bpsk-2/3": 17.87,
    "bpsk-3/4": 18.02,
    "qpsk-1/2": 18.05,
    "qpsk-2/3": 18.38,
    "qpsk-3/4": 17.71,
    "qam16-1/2": 18.25,
    "qam16-2/3": 17.95,
    "qam16-3/4": 18.00,
    "qam64-2/3": 19.71,
    "qam64-3/4": 20.94,
}

#: How much worse each rate's SNR at 10% PER may get than the baseline.
MAX_REGRESSION_DB = 0.25


@pytest.fixture(scope="module")
def curves(fun_ofdm: pathlib.Path) -> dict[str, list[sweep.Point]]:
    """Every rate's curve, from the guardrail's sweep."""
    return sweep.sweep(
        sweep.sim_runner(fun_ofdm, seed=sweep.GUARDRAIL_SEED),
        sweep.RATES,
        sweep.GUARDRAIL_SNRS,
        sweep.GUARDRAIL_FRAMES,
        jobs=max(1, (os.cpu_count() or 2) // 2),
    )


@pytest.mark.parametrize("rate", sweep.RATES)
def test_per_stays_below_5_percent_at_the_fixed_point(
    fun_ofdm: pathlib.Path, rate: str
) -> None:
    point = sweep.sim_runner(fun_ofdm, seed=2)(
        rate, FIXED_SNR_DB[rate], FIXED_FRAMES
    )
    upper = point.interval(Z_99)[1]
    assert upper < MAX_PER, (
        f"{rate} at {point.snr_db} dB lost {point.lost} of {point.sent} "
        f"frames: PER {point.per:.2%}, at most {upper:.2%} (99%)"
    )


@pytest.mark.parametrize("rate", sweep.RATES)
def test_per_never_rises_with_snr(
    curves: dict[str, list[sweep.Point]], rate: str
) -> None:
    assert sweep.is_monotonic(curves[rate]), [
        (p.snr_db, p.per) for p in curves[rate]
    ]


@pytest.mark.parametrize("rate", sweep.RATES)
def test_snr_at_10_percent_per_is_no_worse_than_the_baseline(
    curves: dict[str, list[sweep.Point]], rate: str
) -> None:
    snr = sweep.snr_at_per(curves[rate], sweep.TARGET_PER)
    assert snr is not None, f"{rate} never reached 10% PER"
    assert snr <= BASELINE_SNR_DB[rate] + MAX_REGRESSION_DB, (
        f"{rate} needs {snr:.2f} dB for 10% PER, against "
        f"{BASELINE_SNR_DB[rate]:.2f} dB in the baseline"
    )
