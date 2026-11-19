import os
import pathlib
import sys

import pytest

pytest_plugins = ["pytester"]

REPO = pathlib.Path(__file__).resolve().parents[2]

# A test of the guide, which answers its questions from stdin.
GUIDE_TEST = """
def test_guide(guide):
    guide.say("Hello")
    guide.confirm("Do the thing.")
    assert guide.ask("How many dB?") == "40"
"""


@pytest.fixture
def hil_pytester(
    pytester: pytest.Pytester, monkeypatch: pytest.MonkeyPatch
) -> pytest.Pytester:
    # hil/conftest.py, in a directory of its own, able to import its tools.
    pytester.makeconftest((REPO / "hil" / "conftest.py").read_text())
    paths = [str(REPO / "tools" / "hil"), str(REPO / "tools" / "scorecard")]
    monkeypatch.setenv("PYTHONPATH", os.pathsep.join(paths))
    return pytester


def test_interactive_guide_reads_its_answers_from_stdin(
    hil_pytester: pytest.Pytester,
) -> None:
    hil_pytester.makepyfile(GUIDE_TEST)
    result = hil_pytester.run(
        sys.executable,
        "-m",
        "pytest",
        "--interactive",
        stdin=b"\n40\n",
    )
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*Hello*", "*Do the thing.*"])


def test_guide_asks_nothing_without_interactive(
    hil_pytester: pytest.Pytester,
) -> None:
    hil_pytester.makepyfile(
        """
        def test_guide(guide):
            guide.confirm("Do the thing.")
            assert guide.ask("How many dB?") == ""
        """
    )
    result = hil_pytester.run(sys.executable, "-m", "pytest")
    result.assert_outcomes(passed=1)
    assert "Do the thing." not in result.stdout.str()


# A test that needs the topology.
TOPOLOGY_TEST = """
def test_topology(topology):
    print("Running on the", topology.name, "topology")
"""


def run_topology_test(
    hil_pytester: pytest.Pytester, *args: str, stdin: bytes = b""
) -> pytest.RunResult:
    hil_pytester.makepyfile(TOPOLOGY_TEST)
    return hil_pytester.run(
        sys.executable, "-m", "pytest", "-s", *args, stdin=stdin
    )


def test_topology_is_the_one_named(hil_pytester: pytest.Pytester) -> None:
    result = run_topology_test(hil_pytester, "--topology=loopback")
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*Running on the loopback topology*"])


def test_topology_must_be_named_without_interactive(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_topology_test(hil_pytester)
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*Name the topology with --topology*"])


def test_unsupported_topology_is_refused(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_topology_test(hil_pytester, "--topology=cabled")
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*The cabled topology isn't supported yet*"])


def test_unknown_topology_is_rejected(hil_pytester: pytest.Pytester) -> None:
    result = run_topology_test(hil_pytester, "--topology=star")
    assert result.ret == pytest.ExitCode.USAGE_ERROR
    result.stderr.fnmatch_lines(["*invalid choice*"])


def test_interactive_shows_every_topology_and_asks_for_one(
    hil_pytester: pytest.Pytester,
) -> None:
    # An unknown answer, then cabled, which isn't supported, then 1, which is
    # loopback, then Enter for each of its steps
    result = run_topology_test(
        hil_pytester, "--interactive", stdin=b"star\ncabled\n1\n\n\n"
    )
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(
        [
            "*Which topology do you have?*",
            "1. loopback: one B210, from its channel A to its channel B",
            "*RF A: TX/RX*",
            "2. cabled: two B210s, cabled to each other (not supported yet)",
            "*B210 #1*",
            "*Running on the loopback topology*",
        ]
    )


def test_interactive_takes_a_topology_by_name(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_topology_test(
        hil_pytester, "--interactive", stdin=b"Loopback\n\n\n"
    )
    result.assert_outcomes(passed=1)
    result.stdout.fnmatch_lines(["*Running on the loopback topology*"])


def test_interactive_stops_if_no_topology_is_chosen(
    hil_pytester: pytest.Pytester,
) -> None:
    result = run_topology_test(hil_pytester, "--interactive", stdin=b"x\n")
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*No topology chosen*"])
