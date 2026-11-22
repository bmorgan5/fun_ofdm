"""The receiver's SNR estimate, against the simulated channel's SNR.

sim reports each frame's SNR estimate, from its long training symbols
(src/phy/snr.h). The channel's SNR is known, so the estimate's median must
be within 1 dB of it. Below about 12 dB, timing_sync misses most frames
(see its \\bug), so too few reach the estimate: src/phy/snr_test.cpp tests the
estimate itself from 0 dB.
"""

import json
import pathlib
import subprocess

import pytest

#: The SNRs checked, in dB.
SNRS_DB = (12, 15, 18, 21, 24, 27, 30)

#: Frames at each SNR, and how many must reach the estimate.
FRAMES = 300
MIN_ESTIMATES = 50

#: How far the median estimate may be from the channel's SNR.
MAX_ERROR_DB = 1.0


@pytest.mark.parametrize("snr_db", SNRS_DB)
def test_median_snr_estimate_is_within_1_db(
    fun_ofdm: pathlib.Path, snr_db: int
) -> None:
    done = subprocess.run(
        [
            str(fun_ofdm),
            "sim",
            "--phy-rate=qpsk-1/2",
            f"--snr={snr_db}",
            f"--packets={FRAMES}",
            "--seed=1",
            "--json",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    # sim exits with 1 when it loses frames
    assert done.returncode in (0, 1), done.stderr
    snr = json.loads(done.stdout)["metrics"]["snr"]
    assert snr["qpsk-1/2"]["count"] >= MIN_ESTIMATES, snr["all"]
    assert abs(snr["qpsk-1/2"]["p50"] - snr_db) <= MAX_ERROR_DB, snr["qpsk-1/2"]
