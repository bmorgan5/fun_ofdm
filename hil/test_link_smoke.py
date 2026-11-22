"""Link smoke test: frames over the cable, counted and checked.

It sends 1,000 frames at each of a low, a middle and a high rate (BPSK 1/2,
QAM16 1/2 and QAM64 3/4) from the B210's transmitter to its receiver,
and checks that most get through. The results record losses, duplicates
and overflows.
"""

import hardware
import pytest
from conftest import Link, Results

#: The rates it sends at: low, middle and high.
RATES = ("bpsk-1/2", "qam16-1/2", "qam64-3/4")

#: Frames at each rate.
PACKETS = 1_000

#: The fraction of frames that must arrive intact.
MIN_RECEIVED = 0.95


@pytest.mark.parametrize("rate", RATES)
def test_link_smoke(rate: str, link: Link, results: Results) -> None:
    run = link(rate, rate, PACKETS)
    results.add(run)
    received = run.summary["received"]["packets"] if run.summary else 0
    assert run.status == hardware.FINISHED, results.explain(run)
    assert received >= MIN_RECEIVED * PACKETS, results.explain(run)
