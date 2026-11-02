#!/usr/bin/env bash
# Runs the tests with coverage, writes an HTML report to out/coverage/ and
# prints the line, function and branch coverage. Run it through Bazel, from
# anywhere in the workspace:
#
#   bazel run coverage                            Every test
#   bazel run coverage -- <targets>               Only these tests
#   bazel run coverage -- --baseline <dir> ...    Compare with an earlier report
#
# out/coverage/ also gets the lcov report the HTML is made from (coverage.dat)
# and the commit it's for (commit), so a copy of it can be a baseline later.
# With a baseline, the report and the totals sort the lines by how they've
# changed since the baseline's commit, for example new code that no test runs.

set -euo pipefail

# Bazel starts this in the runfiles tree, where the tool paths (from the BUILD
# file) are relative. Make them absolute before leaving it, and tell the lcov
# wrappers where the tree is, so they can find perl and lcov's modules.
export RUNFILES_DIR="${RUNFILES_DIR:-$(realpath ..)}"
genhtml="$(realpath "${GENHTML}")"
lcov="$(realpath "${LCOV}")"
lcovrc="$(realpath "${LCOVRC}")"
summary_awk="$(realpath "${SUMMARY_AWK}")"

baseline=""
if [[ "${1:-}" == "--baseline" ]]; then
  baseline="$(cd "${BUILD_WORKING_DIRECTORY}" && realpath "$2")"
  shift 2
fi

cd "${BUILD_WORKSPACE_DIRECTORY}"
(($# > 0)) || set -- //...
bazel coverage "$@" >&2

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

# The tests' helpers aren't part of the code they test, so leave them out.
genhtml_args=(--config-file "${lcovrc}" --exclude '*/test_util.h')

# genhtml compares the baseline's lines with the current ones through the
# changes since the baseline's commit, including uncommitted ones. Its paths
# have to match the reports', which have no a/ and b/ prefixes. Copy the
# baseline first, in case it's out/coverage/ itself.
if [[ -n "${baseline}" ]]; then
  cp "${baseline}/coverage.dat" "${tmp}/baseline.dat"
  commit="$(<"${baseline}/commit")"
  git diff --no-prefix "${commit}" >"${tmp}/changes.diff"
  genhtml_args+=(--baseline-file "${tmp}/baseline.dat"
    --diff-file "${tmp}/changes.diff")

  "${lcov}" --config-file "${lcovrc}" --exclude '*/test_util.h' \
    --summary "${tmp}/baseline.dat" >"${tmp}/baseline.txt"
fi

# genhtml reads the sources from the workspace, by the report's relative
# paths. It prints the totals, which go in the table below.
report="$(bazel info output_path 2>/dev/null)/_coverage/_coverage_report.dat"
rm -rf out/coverage
"${genhtml}" "${genhtml_args[@]}" --quiet --output-directory out/coverage \
  "${report}" >"${tmp}/now.txt"
cp "${report}" out/coverage/coverage.dat
git rev-parse HEAD >out/coverage/commit

# The totals, and with a baseline how they've changed, as a table on stdout,
# for CI to pass on. Everything else goes to stderr.
if [[ -n "${baseline}" ]]; then
  awk -f "${summary_awk}" src=baseline commit="${commit}" "${tmp}/baseline.txt" \
    src=now "${tmp}/now.txt"
else
  awk -f "${summary_awk}" src=now "${tmp}/now.txt"
fi
echo "HTML report: out/coverage/index.html" >&2
