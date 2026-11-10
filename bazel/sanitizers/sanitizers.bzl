"""Runtime options for the sanitizer configs in .bazelrc.

A test that might run under a sanitizer sets `env = SANITIZER_ENV` and adds
SANITIZER_DATA to its data: the toolchain's llvm-symbolizer, so reports have
function names and lines, and the leaks to ignore.
"""

_SUPPRESSIONS = "//bazel/sanitizers:lsan_suppressions.txt"
_SYMBOLIZER = "@llvm_toolchain_llvm//:symbolizer"

SANITIZER_DATA = [
    _SUPPRESSIONS,
    _SYMBOLIZER,
]

_SYMBOLIZER_OPTION = "external_symbolizer_path=$(rootpath %s)" % _SYMBOLIZER

SANITIZER_ENV = {
    # FFTW, from the sysroot, has no frame pointers, so ASan's fast unwinder
    # stops inside it. The slow one uses the unwind tables, and gets past it to
    # the suppressed caller.
    "ASAN_OPTIONS": _SYMBOLIZER_OPTION + ":fast_unwind_on_malloc=0",
    "LSAN_OPTIONS": _SYMBOLIZER_OPTION + ":suppressions=$(rootpath %s)" % _SUPPRESSIONS,
    "TSAN_OPTIONS": _SYMBOLIZER_OPTION,
    "UBSAN_OPTIONS": _SYMBOLIZER_OPTION + ":print_stacktrace=1",
}
