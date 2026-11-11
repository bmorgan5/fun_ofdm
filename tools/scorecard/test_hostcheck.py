import dataclasses
import os
import pathlib
from collections.abc import Sequence

import hostcheck
import pytest

INTEL_CPUINFO = """\
processor\t: 0
model name\t: Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz
flags\t\t: fpu sse2 avx2
"""

PI5_CPUINFO = """\
processor\t: 0
BogoMIPS\t: 108.00
CPU implementer\t: 0x41
CPU part\t: 0xd0b
"""


def write(root: pathlib.Path, path: str, text: str) -> None:
    file = root / path.lstrip("/")
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_text(text)


# A host with nothing to warn about, pinned and tuned: what each test changes.
def tuned_host(**changes: object) -> hostcheck.Host:
    host = hostcheck.Host(
        cpu_model="Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz",
        board="",
        arch="x86_64",
        kernel="6.8.0",
        kernel_build="#1 SMP PREEMPT_DYNAMIC",
        preempt_rt=False,
        virtualization="none",
        container="none",
        cpus_online=16,
        cpus_usable=list(range(8, 16)),
        cpu_quota=None,
        governors={"performance": 16},
        turbo=False,
        isolated="",
        nohz_full="",
        load_average=0.1,
        throttled=None,
        on_battery=False,
        compiler="clang version 23.1.4",
        fun_ofdm_version="2.0.0",
        commit="b6254847da79",
        dirty=False,
    )
    return dataclasses.replace(host, **changes)  # type: ignore[arg-type]


def test_cpu_model_is_x86s_model_name() -> None:
    assert hostcheck.cpu_model(INTEL_CPUINFO) == (
        "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz"
    )


def test_cpu_model_names_an_arm_core_from_its_part() -> None:
    assert hostcheck.cpu_model(PI5_CPUINFO) == "ARM Cortex-A76"


def test_cpu_model_numbers_an_unknown_arm_core() -> None:
    cpuinfo = "CPU implementer\t: 0x61\nCPU part\t: 0x022\n"
    assert hostcheck.cpu_model(cpuinfo) == "CPU implementer 0x61, part 0x022"


@pytest.mark.parametrize(
    ("text", "cpus"),
    [
        ("", []),
        ("3", [3]),
        ("0-3", [0, 1, 2, 3]),
        ("0-1,8,10-11", [0, 1, 8, 10, 11]),
    ],
)
def test_cpu_lists_parse_and_format(text: str, cpus: list[int]) -> None:
    assert hostcheck.parse_cpu_list(text) == cpus
    assert hostcheck.format_cpu_list(cpus) == text


def test_cpu_quota_is_the_smallest_of_the_cgroups_limits(
    tmp_path: pathlib.Path,
) -> None:
    write(tmp_path, "/proc/self/cgroup", "0::/a/b\n")
    write(tmp_path, "/sys/fs/cgroup/a/b/cpu.max", "max 100000\n")
    write(tmp_path, "/sys/fs/cgroup/a/cpu.max", "250000 100000\n")
    write(tmp_path, "/sys/fs/cgroup/cpu.max", "400000 100000\n")
    assert hostcheck.cpu_quota(tmp_path) == 2.5


def test_cpu_quota_is_none_without_a_limit(tmp_path: pathlib.Path) -> None:
    write(tmp_path, "/proc/self/cgroup", "0::/\n")
    write(tmp_path, "/sys/fs/cgroup/cpu.max", "max 100000\n")
    assert hostcheck.cpu_quota(tmp_path) is None


def test_governors_counts_each_cpus(tmp_path: pathlib.Path) -> None:
    for cpu, governor in enumerate(["performance", "powersave", "powersave"]):
        write(
            tmp_path,
            f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor",
            governor + "\n",
        )
    assert hostcheck.governors(tmp_path) == {"performance": 1, "powersave": 2}


@pytest.mark.parametrize(
    ("path", "value", "on"),
    [
        ("intel_pstate/no_turbo", "0", True),
        ("intel_pstate/no_turbo", "1", False),
        ("cpufreq/boost", "1", True),
        ("cpufreq/boost", "0", False),
    ],
)
def test_turbo_reads_intels_or_cpufreqs_setting(
    tmp_path: pathlib.Path, path: str, value: str, on: bool
) -> None:
    write(tmp_path, f"/sys/devices/system/cpu/{path}", value + "\n")
    assert hostcheck.turbo(tmp_path) is on


def test_turbo_is_none_without_a_setting(tmp_path: pathlib.Path) -> None:
    assert hostcheck.turbo(tmp_path) is None


def test_virtualization_sees_wsl_and_vms(tmp_path: pathlib.Path) -> None:
    assert (
        hostcheck.virtualization(tmp_path, "6.6.87-microsoft-standard-WSL2")
        == "wsl"
    )
    write(tmp_path, "/proc/cpuinfo", "flags\t: fpu hypervisor\n")
    assert hostcheck.virtualization(tmp_path, "6.8.0") == "vm"


def test_virtualization_is_none_on_bare_metal(tmp_path: pathlib.Path) -> None:
    write(tmp_path, "/proc/cpuinfo", INTEL_CPUINFO)
    assert hostcheck.virtualization(tmp_path, "6.8.0") == "none"


def test_on_battery_when_mains_is_unplugged(tmp_path: pathlib.Path) -> None:
    write(tmp_path, "/sys/class/power_supply/AC/type", "Mains\n")
    write(tmp_path, "/sys/class/power_supply/AC/online", "0\n")
    assert hostcheck.on_battery(tmp_path)
    write(tmp_path, "/sys/class/power_supply/AC/online", "1\n")
    assert not hostcheck.on_battery(tmp_path)


def test_compiler_prefers_clang_to_the_runtimes_gcc(
    tmp_path: pathlib.Path,
) -> None:
    binary = tmp_path / "fun-ofdm"
    binary.write_bytes(
        b"\x7fELF\0GCC: (Ubuntu 15.2.0-16ubuntu1) 15.2.0\0"
        b"clang version 23.1.4 (https://github.com/llvm/llvm-project)\0"
    )
    assert hostcheck.compiler(binary) == "clang version 23.1.4"


def test_compiler_is_none_for_a_missing_binary(tmp_path: pathlib.Path) -> None:
    assert hostcheck.compiler(tmp_path / "missing") is None


def test_an_untuned_machine_is_stock() -> None:
    host = tuned_host(cpus_usable=list(range(16)), governors={"powersave": 16})
    assert hostcheck.configuration(host) == "Stock"


def test_pinned_needs_a_pin_the_performance_governor_and_no_turbo() -> None:
    assert hostcheck.configuration(tuned_host()) == "Pinned"
    assert (
        hostcheck.configuration(tuned_host(cpus_usable=list(range(16))))
        == "Stock"
    )
    assert (
        hostcheck.configuration(tuned_host(governors={"schedutil": 16}))
        == "Stock"
    )
    assert hostcheck.configuration(tuned_host(turbo=True)) == "Stock"


# A Pi has no turbo setting, which doesn't stop it being pinned.
def test_pinned_without_a_turbo_setting() -> None:
    assert hostcheck.configuration(tuned_host(turbo=None)) == "Pinned"


def test_isolated_needs_the_pinned_cpus_isolated_and_tickless() -> None:
    host = tuned_host(isolated="8-15", nohz_full="8-15")
    assert hostcheck.configuration(host) == "Isolated"
    host = tuned_host(isolated="8-15", nohz_full="")
    assert hostcheck.configuration(host) == "Pinned"
    host = tuned_host(isolated="12-15", nohz_full="12-15")
    assert hostcheck.configuration(host) == "Pinned"


def test_rt_needs_isolation_and_a_preempt_rt_kernel() -> None:
    host = tuned_host(isolated="8-15", nohz_full="8-15", preempt_rt=True)
    assert hostcheck.configuration(host) == "RT"
    assert hostcheck.configuration(tuned_host(preempt_rt=True)) == "Pinned"


def test_a_tuned_machine_has_no_warnings() -> None:
    assert hostcheck.warnings(tuned_host()) == []


@pytest.mark.parametrize(
    ("changes", "says"),
    [
        ({"virtualization": "wsl"}, "WSL"),
        ({"virtualization": "vm"}, "a VM"),
        ({"container": "docker", "cpu_quota": 2.5}, "only 2.5 CPUs"),
        ({"cpus_usable": [8, 9]}, "only 2 CPUs"),
        ({"governors": {}}, "can't be read"),
        ({"governors": {"performance": 8, "powersave": 8}}, "powersave on 8"),
        ({"turbo": True}, "intel_pstate/no_turbo"),
        ({"turbo": True, "cpu_model": "AMD Ryzen 9"}, "cpufreq/boost"),
        ({"load_average": 2.0}, "load average is 2.00"),
        ({"isolated": "12-15"}, "Pin it to them: taskset -c 12-15"),
        ({"throttled": "0x50005"}, "throttled"),
        ({"on_battery": True}, "battery"),
        ({"commit": None}, "isn't a git checkout"),
        ({"dirty": True}, "uncommitted changes"),
        ({"compiler": None}, "compiler"),
    ],
)
def test_warns_about_what_skews_results(
    changes: dict[str, object], says: str
) -> None:
    found = hostcheck.warnings(tuned_host(**changes))
    assert len(found) == 1
    assert says in found[0]


# In a VM, a missing governor is part of the VM's warning, not its own.
def test_a_vm_without_cpufreq_gets_one_warning() -> None:
    assert (
        len(hostcheck.warnings(tuned_host(virtualization="vm", governors={})))
        == 1
    )


def test_collect_reads_the_machine(tmp_path: pathlib.Path) -> None:
    write(tmp_path, "/proc/cpuinfo", PI5_CPUINFO)
    write(tmp_path, "/proc/loadavg", "0.25 0.20 0.15 1/300 4000\n")
    write(tmp_path, "/sys/devices/system/cpu/online", "0-3\n")
    write(tmp_path, "/sys/devices/system/cpu/isolated", "2-3\n")
    write(tmp_path, "/sys/devices/system/cpu/nohz_full", "2-3\n")
    write(
        tmp_path,
        "/sys/firmware/devicetree/base/model",
        "Raspberry Pi 5 Model B Rev 1.0\0",
    )
    for cpu in range(4):
        write(
            tmp_path,
            f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor",
            "performance\n",
        )
    outputs = {
        "vcgencmd": "throttled=0x0\n",
        "git": "abc123\n",
    }

    def run(command: Sequence[str]) -> str | None:
        if command[0].endswith("fun-ofdm"):
            return "2.0.0\n"
        if command[:4] == ["git", "-C", str(hostcheck.REPO), "status"]:
            return ""
        return outputs.get(command[0])

    host = hostcheck.collect(
        tmp_path,
        tmp_path / "fun-ofdm",
        uname=os.uname_result(
            ("Linux", "pi", "6.12.0-rt", "#1 SMP PREEMPT_RT", "aarch64")
        ),
        affinity={2, 3},
        run=run,
    )
    assert host.cpu_model == "ARM Cortex-A76"
    assert host.board == "Raspberry Pi 5 Model B Rev 1.0"
    assert host.arch == "aarch64"
    assert host.preempt_rt
    assert host.cpus_online == 4
    assert host.cpus_usable == [2, 3]
    assert host.load_average == 0.25
    assert host.throttled == "0x0"
    assert host.fun_ofdm_version == "2.0.0"
    assert host.commit == "abc123"
    assert not host.dirty
    assert host.configuration == "RT"
    # Pinned to 2 CPUs, and fun-ofdm isn't really there.
    assert [warning.split(",")[0] for warning in host.warnings] == [
        "This process may use only 2 CPUs",
        "The compiler that built fun-ofdm isn't known.",
    ]


def test_report_lists_the_machine_and_its_warnings() -> None:
    host = tuned_host(dirty=True)
    host.configuration = hostcheck.configuration(host)
    host.warnings = hostcheck.warnings(host)
    text = hostcheck.report(host)
    assert "Configuration  Pinned" in text
    assert "CPUs usable    8-15 of 16" in text
    assert "Warnings:\n- The work tree has uncommitted changes" in text
