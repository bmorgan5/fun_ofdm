import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]


def test_python_is_the_version_in_python_version_file() -> None:
    pinned = (REPO / ".python-version").read_text().strip()
    running = f"{sys.version_info.major}.{sys.version_info.minor}"
    assert running == pinned
