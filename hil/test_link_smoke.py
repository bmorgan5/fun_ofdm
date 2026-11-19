"""Link smoke test: frames over the topology's link, counted and checked.

It sends 10,000 frames at BPSK 1/2 from the topology's transmitter to its
receiver, and checks every byte. It records losses, duplicates, overflows,
false detections and sample-arrival jitter: the Milestone 2 baseline. After
Milestone 3's fixes, nothing should be lost.
"""

from typing import Any

import hardware
import pytest
from conftest import Link, Results

#: Frames in the baseline run.
PACKETS = 10_000

#: Frames in the QAM16 run, which checks that each burst's end gets through.
QAM16_PACKETS = 1_000

#: Frames in the check that the link works at all.
CHECK_PACKETS = 20

#: The fraction of frames that must arrive: the README's "the setup works".
MIN_RECEIVED = 0.9


# A precondition, so it errors rather than fails: do frames get through?
@pytest.fixture(scope="module")
def connected(link: Link) -> None:
    received = link("bpsk-1/2", CHECK_PACKETS)["received"]["packets"]
    if received < CHECK_PACKETS // 2:
        pytest.fail(
            f"Only {received} of {CHECK_PACKETS} frames got through: check the "
            "cables and the gains"
        )


@pytest.fixture(scope="module")
def bpsk(link: Link, connected: None, results: Results) -> dict[str, Any]:
    summary = link("bpsk-1/2", PACKETS)
    results.add("bpsk-1/2", summary, hardware.loopback_row(summary))
    return summary


def test_most_frames_get_through(bpsk: dict[str, Any]) -> None:
    assert bpsk["received"]["packets"] >= MIN_RECEIVED * PACKETS


def test_the_frames_received_are_intact(bpsk: dict[str, Any]) -> None:
    assert bpsk["mismatched_bytes"] == 0


@pytest.mark.skip(
    reason="Milestone 3: the USRP overflows when it starts, and frames go "
    "undetected (src/usrp/usrp.h, src/phy/timing_sync.h)"
)
def test_no_frame_is_lost_or_duplicated(bpsk: dict[str, Any]) -> None:
    assert (bpsk["lost"], bpsk["duplicates"]) == (0, 0)


def test_qam16_gets_through(
    link: Link, connected: None, results: Results
) -> None:
    summary = link("qam16-1/2", QAM16_PACKETS)
    results.add("qam16-1/2", summary, hardware.loopback_row(summary))
    assert summary["received"]["packets"] >= MIN_RECEIVED * QAM16_PACKETS
    assert summary["mismatched_bytes"] == 0
