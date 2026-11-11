# Scorecard

The same measurements of the receiver every time, so a release's numbers
compare directly with the last one's. [hostcheck.py](hostcheck.py) records the
machine they run on.

```bash
bazel run install
uv run tools/scorecard/hostcheck.py         # The machine, and what to fix first
uv run tools/scorecard/hostcheck.py --json
```

## Preparing the machine

Numbers from a busy machine, or one that changes its own clocks, aren't worth
comparing. A configuration is how the machine is set up: there are four, each
adding to the one before. They aren't settings of fun-ofdm or of these
scripts. You set the machine up, and hostcheck works out which configuration
it's in and warns about what's left.

| Configuration | What's set up                                                                     | Where                                          |
|---------------|-----------------------------------------------------------------------------------|------------------------------------------------|
| Stock         | Nothing                                                                           |                                                |
| Pinned        | fun-ofdm pinned to some CPUs, every CPU on the `performance` governor, turbo off  | `taskset`, and `/sys`, until a reboot          |
| Isolated      | The pinned CPUs kept free of other work, the timer tick and interrupts            | The kernel's command line, then a reboot       |
| RT            | A `PREEMPT_RT` kernel, and real-time thread priorities                            | Installing that kernel                         |

These are examples, for CPUs 2 to 7 of an 8-core x86 machine and CPUs 1 to 3
of a Raspberry Pi 5: pick the CPUs for yours.

**x86:**

```bash
# Pinned, until the next reboot. Then run with taskset -c 2-7.
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo  # Intel turbo
echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost          # AMD boost

# Isolated: add this to GRUB_CMDLINE_LINUX in /etc/default/grub, then run
# sudo update-grub and reboot. Interrupts go to CPUs 0 and 1.
isolcpus=2-7 nohz_full=2-7 irqaffinity=0-1
```

**Raspberry Pi 5:** it has no turbo setting, but it slows down when it gets
hot, so it needs a cooler; hostcheck warns if it has been throttled. With 4
cores, the receiver's 7 threads always share them.

```bash
# Pinned, until the next reboot. Then run with taskset -c 1-3.
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Isolated: add this to the end of the one line in /boot/firmware/cmdline.txt,
# and reboot. Interrupts go to CPU 0.
isolcpus=1-3 nohz_full=1-3 irqaffinity=0
```

Throughput is measured on Pinned or later. Latency and jitter are measured on
each configuration, since tuning mostly changes the tail, not the typical
value.

In a VM or WSL, the hypervisor owns the clocks and shares the CPUs, so the
numbers only compare with others from the same machine.

## TODO

- **Real-time priorities in `sim` and `fun-ofdm-bench`.** Only `rx` and `tx`
  ask for them, so the RT configuration only adds the kernel for now.
- **Check the interrupts.** hostcheck doesn't check that interrupts stay off
  the isolated CPUs.
- **Record the compiler flags.** The binary records its compiler, not its
  flags, so a result can't say whether it was an optimized build.
