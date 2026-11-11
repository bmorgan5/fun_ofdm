"""Options and fixtures for the tests of fun-ofdm: the binary they run."""

import pathlib
import platform

import pytest

REPO = pathlib.Path(__file__).resolve().parents[1]


def pytest_addoption(parser: pytest.Parser) -> None:
    """Adds --fun-ofdm, the binary that the tests run."""
    parser.addoption(
        "--fun-ofdm",
        type=pathlib.Path,
        # As tools/per/sweep.py: the one `bazel run install` copies.
        default=REPO
        / "out"
        / f"linux_{platform.machine()}"
        / "bin"
        / "fun-ofdm",
        help="the fun-ofdm binary to test",
    )


@pytest.fixture(scope="session")
def fun_ofdm(request: pytest.FixtureRequest) -> pathlib.Path:
    """The fun-ofdm binary. Fails the test if it hasn't been installed."""
    path: pathlib.Path = request.config.getoption("--fun-ofdm")
    if not path.exists():
        pytest.fail(f"{path} doesn't exist: bazel run install")
    return path
