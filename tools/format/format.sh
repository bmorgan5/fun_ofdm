#!/usr/bin/env bash
# Formats the C++ and Starlark files with the pinned clang-format and
# buildifier. Run it through Bazel, from anywhere in the workspace:
#
#   bazel run format                 Format every file under the current directory
#   bazel run format -- <files>      Format only these files
#   bazel run format.check           Change nothing; fail if any file needs formatting
#
# --check also works with files: `bazel run format.check -- <files>`.

set -euo pipefail

# Bazel starts this in the runfiles tree, where the tool paths (from the BUILD
# file) are relative. Make them absolute before leaving it, and tell the
# buildifier wrapper where the tree is, so it can find the real binary.
export RUNFILES_DIR="${RUNFILES_DIR:-$(realpath ..)}"
clang_format="$(realpath "${CLANG_FORMAT}")"
buildifier="$(realpath "${BUILDIFIER}")"
cd "${BUILD_WORKING_DIRECTORY}"

check=false
if [[ "${1:-}" == "--check" ]]; then
  check=true
  shift
fi

# The arguments, or every tracked or new (but not ignored) file.
if (($# > 0)); then
  files=("$@")
else
  mapfile -t files < <(git ls-files --cached --others --exclude-standard)
fi

cc_files=()
starlark_files=()
for file in "${files[@]}"; do
  [[ -f "${file}" ]] || continue # Deleted, but not yet staged
  case "${file}" in
    *.cpp | *.h) cc_files+=("${file}") ;;
    *.bazel | *.bzl | BUILD | */BUILD) starlark_files+=("${file}") ;;
  esac
done

status=0
if ((${#cc_files[@]} > 0)); then
  if ${check}; then
    # Report the first problem in each file rather than every line.
    "${clang_format}" --dry-run -Werror --ferror-limit=1 "${cc_files[@]}" || status=1
  else
    "${clang_format}" -i "${cc_files[@]}"
  fi
fi
if ((${#starlark_files[@]} > 0)); then
  if ${check}; then
    "${buildifier}" -mode=check "${starlark_files[@]}" || status=1
  else
    "${buildifier}" -mode=fix "${starlark_files[@]}"
  fi
fi

if ((status != 0)); then
  echo "Some files need formatting. Run 'bazel run format' to fix them." >&2
fi
exit "${status}"
