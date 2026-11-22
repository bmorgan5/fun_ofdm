import dataclasses
import hashlib
import json
import pathlib

import hostcheck
import pytest
import result

HOST = hostcheck.Host(
    cpu_model="ARM Cortex-A76",
    board="Raspberry Pi 5 Model B Rev 1.0",
    arch="aarch64",
    kernel="6.8.0-1068-raspi",
    kernel_build="#72-Ubuntu SMP PREEMPT_DYNAMIC",
    preempt_rt=False,
    virtualization="none",
    container="none",
    cpus_online=4,
    cpus_usable=[1, 2, 3],
    cpu_quota=None,
    governors={"performance": 4},
    turbo=None,
    isolated="",
    nohz_full="",
    load_average=0.1,
    throttled="0x0",
    on_battery=False,
    compiler="clang version 23.1.4",
    fun_ofdm_version="2.1.0",
    commit="0123456789abcdef",
    dirty=False,
    configuration="Pinned",
)


def binary(tmp_path: pathlib.Path, content: bytes = b"code") -> pathlib.Path:
    path = tmp_path / "fun-ofdm"
    path.write_bytes(content)
    return path


def test_hashes_a_file(tmp_path: pathlib.Path) -> None:
    path = binary(tmp_path)
    assert result.sha256(path) == hashlib.sha256(b"code").hexdigest()


def test_makes_options_json() -> None:
    options = {"out": pathlib.Path("/x"), "rates": ("a", "b"), "n": {"k": 1}}
    assert result.jsonable(options) == {
        "out": "/x",
        "rates": ["a", "b"],
        "n": {"k": 1},
    }


def test_provenance_has_the_tool_binaries_host_and_hardware(
    tmp_path: pathlib.Path,
) -> None:
    path = binary(tmp_path)
    prov = result.provenance(
        "tools/per/sweep.py",
        {"frames": 500, "out": tmp_path},
        {"fun-ofdm": path},
        {"serial": "F61912"},
        host=HOST,
    )
    assert list(prov) == [
        "tool",
        "options",
        "date",
        "binaries",
        "sources",
        "host",
        "hardware",
    ]
    assert prov["options"] == {"frames": 500, "out": str(tmp_path)}
    assert prov["binaries"] == {
        "fun-ofdm": {"path": str(path), "sha256": result.sha256(path)}
    }
    assert prov["host"] == hostcheck.to_json(HOST)
    assert prov["hardware"] == {"serial": "F61912"}


def test_provenance_without_hardware(tmp_path: pathlib.Path) -> None:
    prov = result.provenance("t", {}, {"fun-ofdm": binary(tmp_path)}, host=HOST)
    assert prov["hardware"] is None


def test_describes_what_made_a_result(tmp_path: pathlib.Path) -> None:
    prov = result.provenance(
        "t",
        {},
        {"fun-ofdm": binary(tmp_path)},
        {"serial": "F61912", "tx_gain": 80},
        host=dataclasses.replace(HOST, dirty=True),
    )
    prov["date"] = "2026-11-22T00:00:00+00:00"
    digest = result.sha256(binary(tmp_path))[:12]
    assert result.describe(prov) == (
        "2026-11-22T00:00:00+00:00; fun-ofdm 2.1.0 at commit 0123456789ab "
        f"with uncommitted changes; SHA-256 fun-ofdm {digest}; "
        "Raspberry Pi 5 Model B Rev 1.0, Pinned; serial F61912, tx_gain 80."
    )


def test_writes_and_reads_a_result_with_its_provenance_first(
    tmp_path: pathlib.Path,
) -> None:
    prov = result.provenance("t", {}, {"fun-ofdm": binary(tmp_path)}, host=HOST)
    out = tmp_path / "per"
    result.write(out, prov, {"rates": {"bpsk-1/2": 18.0}}, "PER", "| a |\n")
    text = (out / "result.json").read_text()
    assert list(json.loads(text)) == ["provenance", "rates"]
    assert result.read(out) == {
        "provenance": prov,
        "rates": {"bpsk-1/2": 18.0},
    }
    assert (out / "result.md").read_text() == (
        f"# PER\n\n{result.describe(prov)}\n\n| a |\n"
    )


def test_writes_a_markdown_table_with_its_columns_lined_up() -> None:
    rows = [
        {"Run": "bpsk-1/2", "Lost": "3"},
        {"Run": "qam16-1/2", "Lost": "12"},
    ]
    assert result.markdown_table(rows) == (
        "| Run       | Lost |\n"
        "| --------- | ---- |\n"
        "| bpsk-1/2  | 3    |\n"
        "| qam16-1/2 | 12   |\n"
    )


def test_writes_no_table_without_rows() -> None:
    assert result.markdown_table([]) == ""


@pytest.fixture
def prov(tmp_path: pathlib.Path) -> dict:
    return result.provenance(
        "t", {"frames": 500}, {"fun-ofdm": binary(tmp_path)}, host=HOST
    )


def test_a_result_with_the_same_provenance_can_be_reused(prov: dict) -> None:
    later = {**prov, "date": "later", "host": {**prov["host"], "load": 3}}
    assert result.reuse_problems(prov, later) == []


def test_a_changed_binary_cant_be_reused(prov: dict) -> None:
    later = {
        **prov,
        "binaries": {"fun-ofdm": {"path": "/y", "sha256": "beef"}},
    }
    assert result.reuse_problems(prov, later) == ["fun-ofdm has changed"]


def test_a_binary_the_result_didnt_run_cant_be_reused(prov: dict) -> None:
    later = {
        **prov,
        "binaries": {
            **prov["binaries"],
            "fun-ofdm-bench": {"path": "/b", "sha256": "beef"},
        },
    }
    assert result.reuse_problems(prov, later) == [
        "it didn't run fun-ofdm-bench"
    ]


def test_a_host_set_up_differently_cant_be_reused(prov: dict) -> None:
    later = {**prov, "host": {**prov["host"], "configuration": "Stock"}}
    assert result.reuse_problems(prov, later) == [
        "the host's configuration was 'Pinned', and is now 'Stock'"
    ]


def test_other_options_cant_be_reused(prov: dict) -> None:
    later = {**prov, "options": {"frames": 1000}}
    assert result.reuse_problems(prov, later) == ["its options differ"]


def test_options_are_compared_only_if_given(prov: dict) -> None:
    later = {k: v for k, v in prov.items() if k != "options"}
    assert result.reuse_problems(prov, later) == []


def test_main_says_which_results_can_be_reused(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    for name in result.BINARIES:
        (bin_dir / name).write_bytes(b"code")
    monkeypatch.setattr(hostcheck, "collect", lambda **_: HOST)
    same = result.provenance(
        "t", {}, {"fun-ofdm": bin_dir / "fun-ofdm"}, host=HOST
    )
    result.write(tmp_path / "same", same, {}, "T", "")
    changed = {
        **same,
        "binaries": {"fun-ofdm": {"path": "/x", "sha256": "beef"}},
    }
    result.write(tmp_path / "changed", changed, {}, "T", "")
    code = result.main(
        [
            str(tmp_path / "same"),
            str(tmp_path / "changed"),
            "--bin-dir",
            str(bin_dir),
        ]
    )
    assert code == 1
    out = capsys.readouterr().out
    assert f"{tmp_path / 'same'}: can be reused" in out
    assert f"{tmp_path / 'changed'}: measure again: fun-ofdm has changed" in out


def test_provenance_reads_the_host_with_fun_ofdm(
    tmp_path: pathlib.Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    seen = []

    def collect(fun_ofdm: pathlib.Path) -> hostcheck.Host:
        seen.append(fun_ofdm)
        return HOST

    monkeypatch.setattr(hostcheck, "collect", collect)
    bench = tmp_path / "fun-ofdm-bench"
    bench.write_bytes(b"bench")
    fun_ofdm = binary(tmp_path)
    result.provenance("t", {}, {"fun-ofdm-bench": bench, "fun-ofdm": fun_ofdm})
    assert seen == [fun_ofdm]


def test_main_needs_only_the_binaries_the_results_ran(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    (bin_dir / "fun-ofdm").write_bytes(b"code")
    monkeypatch.setattr(hostcheck, "collect", lambda **_: HOST)
    prov = result.provenance(
        "t",
        {},
        {"fun-ofdm": bin_dir / "fun-ofdm"},
        {"serial": "F61912"},
        host=HOST,
    )
    result.write(tmp_path / "hil", prov, {}, "T", "")
    code = result.main([str(tmp_path / "hil"), "--bin-dir", str(bin_dir)])
    assert code == 0
    assert "can be reused (not its hardware)" in capsys.readouterr().out


def test_provenance_hashes_the_tools_sources() -> None:
    source = pathlib.Path(result.__file__)
    prov = result.provenance("t", {}, {}, host=HOST, sources=[source])
    assert prov["sources"] == {"tools/results/result.py": result.sha256(source)}


def test_a_changed_source_cant_be_reused(prov: dict) -> None:
    old = {**prov, "sources": {"tools/per/sweep.py": "abc"}}
    new = {**prov, "sources": {"tools/per/sweep.py": "def"}}
    assert result.reuse_problems(old, new) == ["tools/per/sweep.py has changed"]


def test_main_says_to_measure_a_directory_without_provenance_again(
    tmp_path: pathlib.Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    monkeypatch.setattr(hostcheck, "collect", lambda **_: HOST)
    (tmp_path / "old").mkdir()
    (tmp_path / "old" / "result.json").write_text('{"rates": {}}')
    code = result.main([str(tmp_path / "old"), str(tmp_path / "none")])
    assert code == 1
    out = capsys.readouterr().out
    assert f"{tmp_path / 'old'}: measure again: it has no result.json" in out
    assert f"{tmp_path / 'none'}: measure again: it has no result.json" in out


def test_writes_arrays_of_numbers_on_one_line() -> None:
    text = result.to_json_text({"buckets": [[100, 2], [110, 3]], "s": ["a"]})
    assert text == (
        '{\n  "buckets": [\n    [100, 2],\n    [110, 3]\n  ],\n'
        '  "s": [\n    "a"\n  ]\n}\n'
    )


def test_leaves_strings_and_mixed_arrays_alone() -> None:
    data = {"s": "x [1,   2] y", "m": [1, "a"], "e": [], "b": [True, 1]}
    text = result.to_json_text(data)
    assert json.loads(text) == data
    assert '"x [1,   2] y"' in text
    assert '"m": [\n    1,\n    "a"\n  ]' in text
