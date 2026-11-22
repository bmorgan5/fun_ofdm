# Releasing

Each milestone ends with a release: a tag, and a page on GitHub with the
release notes, the binaries and the measurements. This is the procedure, for a
person or an agent. The milestone's last PR does steps 1 to 5, and its
reviewer steps 6 and 7.

The raw results stay out of git: they go in `out/results/vX.Y.Z/`, and are
attached to the release as `results-vX.Y.Z.tar.gz`. The repo keeps only the
release notes and the charts they show, in `docs/releases/vX.Y.Z/`.

## 1. Prepare the Pi 5 and the B210

Releases are measured on a Raspberry Pi 5 with a B210, cabled as in
[hil/README.md](../hil/README.md), so each release compares with the last.

1. Bump `version` in `MODULE.bazel`, which the binaries' `--version`
   reports, build once (`bazel build //...` updates `MODULE.bazel.lock`), and
   commit both with the release PR's last change to the code. With no
   uncommitted changes, build: `bazel run install`. Every result records the
   binaries' SHA-256 and version, and the commit, and a dirty tree shows up
   in the scorecard. The rebase merge rewrites the commit, so the SHA-256 is
   what pins a result to its binaries.
2. Close everything else, then run `uv run tools/scorecard/hostcheck.py` and
   fix what it warns about. At least, set the Pinned configuration
   ([tools/scorecard/README.md](../tools/scorecard/README.md#preparing-the-machine)):
   the `performance` governor, and `--cpus 1-3` for the measurements that
   take it, leaving CPU 0 for the rest of the system.
3. Run the hardware tests once, `uv run pytest hil`, to check the cabling and
   to warm the B210 up.

## 2. Measure

Each measurement writes a directory of results under `out/results/vX.Y.Z/`
with `--out` ([the result contract](../tools/results/result.py)). Run them in
this order, with the Pi doing nothing else: they take about 2 hours. Set
`M=out/results/vX.Y.Z` first.

| Directory       | Command                                                                                                        | Takes  |
|-----------------|----------------------------------------------------------------------------------------------------------------|--------|
| `tests/`        | `uv run tools/scorecard/measure.py tests --out $M/tests`                                                       | 5 min  |
| `per/`          | `uv run tools/per/sweep.py --snr-min 14 --snr-max 24 --snr-step 0.5 --frames 1000 --out $M/per`                | 7 min  |
| `latency/`      | `uv run tools/scorecard/measure.py latency --cpus 1-3 --out $M/latency`                                        | 3 min  |
| `bench/`        | `uv run tools/scorecard/measure.py bench --cpus 1-3 --out $M/bench`                                            | 50 min |
| `link_smoke/`   | `uv run pytest hil --out $M`                                                                                   | 1 min  |
| `realtime/`     | `uv run tools/hardware/hardware_sweep.py realtime --out $M/realtime`                                           | 8 min  |
| `per-hardware/` | `uv run tools/hardware/hardware_sweep.py per --sim $M/per --out $M/per-hardware`                               | 13 min |

The PER sweep uses the guardrail's settings, so its numbers are the ones
`tests/test_per.py` checks. The hardware sweeps pin nothing: the receiver's 7
threads need every core.

Then draw the charts and write the scorecard, against the last release's
results, if it has any, from its page:

```bash
uv run tools/scorecard/charts.py $M
gh release download vPREV --pattern 'results-*.tar.gz' --dir out/results
tar -C out/results -xzf out/results/results-vPREV.tar.gz
uv run tools/scorecard/scorecard.py $M --compare out/results/vPREV
```

**Reusing a result:** `uv run tools/results/result.py --cpus 1-3
$M/{bench,latency}` and `uv run tools/results/result.py
$M/{tests,per,link_smoke,realtime,per-hardware}` say
which results were measured with the binaries, tool sources and machine setup
that would run now. One that matches can stay, but measure the hardware ones
again on the day of the release anyway: nothing records a reseated cable, a
warmer room or a different attenuator. Measure everything again after any
change to `src/`.

## 3. Check the measurements

A few more checks say how far to trust the scorecard. Each measurement goes
in a directory of its own under `$M`, which `scorecard.py $M` doesn't read.

**Reproducibility:** measure the repeated rows again, and compare. They agree
within noise if almost every row is "same": with dozens of rows each tested
at p < 0.05, a few differ by chance. Report how many:

```bash
uv run tools/scorecard/measure.py bench --cpus 1-3 --out $M/repeat/bench
uv run tools/scorecard/measure.py latency --cpus 1-3 --out $M/repeat/latency
uv run tools/scorecard/scorecard.py $M/repeat --compare $M
```

**What the metrics cost:** the receiver chain's throughput built without
metrics, with them, and with a trace, on 2 s workloads, 10 times each:

```bash
bazel run --config=no_metrics //:install -- --destdir=$PWD/out/no-metrics
bench="uv run tools/scorecard/measure.py bench --cpus 1-3 --filter ^receiver_chain/ --workload-seconds 2 --repetitions 10"
$bench --bin-dir out/no-metrics/bin --out $M/overhead/none/bench
$bench --out $M/overhead/histograms/bench
$bench --trace /tmp/bench.pftrace --out $M/overhead/trace/bench
uv run tools/scorecard/scorecard.py $M/overhead/histograms --compare $M/overhead/none
uv run tools/scorecard/scorecard.py $M/overhead/trace --compare $M/overhead/histograms
rm /tmp/bench.pftrace
```

**Metrics agree:** `$M/charts/charts.md` compares each traced run's histogram
percentiles with its trace's.

## 4. Write the release notes

Write `docs/releases/vX.Y.Z/README.md`, and copy the charts it shows from
`$M/charts/` beside it: only the ones the notes need, since every chart is in
the results archive too. Its first heading is the release's title, like
`# v2.1.0 Measure`, which the release workflow checks. Link the repo's files
and the charts with plain relative links, `[text](path)` or
`![text](image.png)`: the workflow points them at the tag, so they work on
the release page too. It doesn't rewrite reference-style links or links with
a title. From v2.1.0, every release has these
sections:

1. **Headline:** a sentence or two with the main result.
2. **What's new:** the changes a user sees.
3. **Results:** the scorecard's headline numbers, with the last release's and
   the change, and the charts that explain them.
4. **What made the difference:** from Milestone 3 on, a row per fix or
   optimization, with its own before and after.
5. **How it was measured:** a short summary of the machine, the setup and
   what each measurement ran, not the commands, which are here.
6. **Known bugs** and differences from 802.11a, as in
   [v2.0.0's notes](releases/v2.0.0/README.md).
7. **Links:** each issue and its PR, and the diff since the last release
   (`compare/vPREV...vX.Y.Z`).

## 5. Finish the PR

In the milestone's last PR:

- set `RELEASE` in the README's Quick start;
- add a row to the README's Results table (the first release adds the table),
  linking
  `https://github.com/bmorgan5/fun_ofdm/releases/tag/vX.Y.Z`;
- commit `docs/releases/vX.Y.Z/`, but not `$M`.

Then pack the results, to attach after the tag:

```bash
tar -C out/results -czf out/results/results-vX.Y.Z.tar.gz vX.Y.Z
```

## 6. Tag

Merge the PR (by rebase), then tag the merged commit on `main`:

```bash
git fetch origin && git checkout main && git pull --ff-only
git tag -a vX.Y.Z -m "vX.Y.Z <name>"
git push origin vX.Y.Z
```

The tag starts [the release workflow](../.github/workflows/release.yml). It
checks the tag against `MODULE.bazel`, runs the tests, and makes a draft
release with the notes, an archive for each architecture, `SHA256SUMS` and
the build attestations.

## 7. Publish

Attach the results to the draft, then read it on GitHub as a visitor would,
and publish it:

```bash
gh release upload vX.Y.Z out/results/results-vX.Y.Z.tar.gz
```
