"""Builds per-architecture sysroots out of Debian packages from an apt archive.

Each `sysroot.deb` tag creates a repository holding the headers and libraries
of a set of packages (plus everything they depend on) for one architecture.
The packages are downloaded from an apt archive, normally a dated snapshot so
the sysroot never changes, and unpacked by Bazel itself. Nothing is installed
on the host.

Resolving the packages means reading the archive's indexes, which takes a
while. Bazel records the result (each .deb's URL and checksum) in
MODULE.bazel.lock and only resolves again when a tag changes. The .debs are
kept in Bazel's repository cache, so each is downloaded once per machine.
"""

# dpkg architecture -> Debian multiarch tuple.
_MULTIARCH = {
    "amd64": "x86_64-linux-gnu",
    "arm64": "aarch64-linux-gnu",
}

# The index fields the resolver needs.
_FIELDS = ["Package", "Provides", "Pre-Depends", "Depends", "Filename", "SHA256"]

_SYSROOT_BUILD = """\
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sysroot",
    srcs = ["."],
)

# For the library targets in the repository's root package.
exports_files(glob(["usr/lib/**"]))
"""

def _parse_index(
        text: str,
        packages: dict[str, dict[str, str]],
        provides: dict[str, list[str]]) -> None:
    """Adds a Packages index to `packages` (name -> fields) and `provides` (virtual name -> names).

    A package already in `packages` is replaced, so later indexes take precedence.
    """
    for stanza in text.split("\n\n"):
        fields = {}
        for line in stanza.split("\n"):
            key, sep, value = line.partition(": ")
            if sep and key in _FIELDS:
                fields[key] = value
        name = fields.get("Package")
        if not name:
            continue
        packages[name] = fields
        for virtual in fields.get("Provides", "").split(","):
            virtual = virtual.split("(")[0].strip()
            if virtual and name not in provides.get(virtual, []):
                provides.setdefault(virtual, []).append(name)

def _excluded(name: str, patterns: list[str]) -> bool:
    for pattern in patterns:
        if pattern.endswith("*"):
            if name.startswith(pattern[:-1]):
                return True
        elif name == pattern:
            return True
    return False

def _resolve(
        atom: str,
        packages: dict[str, dict[str, str]],
        provides: dict[str, list[str]],
        selected: dict[str, bool]) -> str | None:
    """Returns the package satisfying a dependency atom, e.g. `libfoo (>= 1.0)`, or None.

    Versions are ignored: an archive snapshot is consistent, so its newest
    version satisfies the other packages in it. A virtual package resolves to
    a provider that is already selected, if there is one.
    """
    name = atom.split("(")[0].strip().split(":")[0]
    if name in packages:
        return name
    providers = provides.get(name, [])
    for provider in providers:
        if provider in selected:
            return provider
    return providers[0] if providers else None

def _closure(
        tag: bazel_module_tag,
        packages: dict[str, dict[str, str]],
        provides: dict[str, list[str]]) -> list[str]:
    """Returns the requested packages plus their transitive Depends/Pre-Depends."""
    selected = {}
    pending = []
    for package in tag.packages:
        resolved = _resolve(package, packages, provides, selected)
        if not resolved:
            fail("Package {} not found for {} in {}".format(package, tag.arch, tag.mirror))
        selected[resolved] = True
        pending.append(resolved)

    # Starlark has no while loops; the closure can't be larger than the index.
    for _ in range(len(packages)):
        if not pending:
            break
        fields = packages[pending.pop(0)]
        depends = fields.get("Pre-Depends", "") + "," + fields.get("Depends", "")
        for dep in depends.split(","):
            # For alternatives ("a | b"), take the first one in the archive.
            for alternative in dep.split("|"):
                if not alternative.strip():
                    continue
                resolved = _resolve(alternative, packages, provides, selected)
                if not resolved:
                    continue
                if resolved not in selected and not _excluded(resolved, tag.exclude_packages):
                    selected[resolved] = True
                    pending.append(resolved)
                break
    return sorted(selected.keys())

def _deb_sysroot_impl(rctx: repository_ctx) -> None:
    multiarch = _MULTIARCH[rctx.attr.arch]

    for i, (url, sha256) in enumerate(rctx.attr.debs.items()):
        rctx.report_progress("Unpacking {} of {} packages".format(i + 1, len(rctx.attr.debs)))
        deb = "debs/{}".format(i)
        rctx.download_and_extract(url, output = deb, sha256 = sha256, type = "deb")
        data = [f for f in rctx.path(deb).readdir() if f.basename.startswith("data.tar")]
        rctx.extract(data[0], output = "sysroot")
        rctx.delete(deb)
    rctx.delete("debs")

    result = rctx.execute([rctx.path(rctx.attr._fixup), "sysroot", multiarch])
    if result.return_code:
        fail("Fixing up sysroot failed: " + result.stderr)

    # The sysroot is a source directory so the toolchain stages it with one
    # symlink instead of one per file.
    rctx.file("sysroot/BUILD.bazel", _SYSROOT_BUILD, executable = False)
    rctx.file("packages.txt", "".join([url.rpartition("/")[2] + "\n" for url in rctx.attr.debs]), executable = False)
    rctx.file("multiarch.bzl", 'MULTIARCH = "{}"\n'.format(multiarch), executable = False)
    rctx.symlink(rctx.attr.build_file, "BUILD.bazel")

deb_sysroot = repository_rule(
    implementation = _deb_sysroot_impl,
    doc = "A sysroot made of the headers and libraries in a set of .deb packages.",
    attrs = {
        "arch": attr.string(
            mandatory = True,
            values = _MULTIARCH.keys(),
            doc = "dpkg architecture of the sysroot.",
        ),
        "build_file": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = ("BUILD file for the repository's root package. The sysroot itself is " +
                   "`//sysroot`, with its files in that package, and `MULTIARCH` can be " +
                   "loaded from `:multiarch.bzl`."),
        ),
        "debs": attr.string_dict(
            mandatory = True,
            doc = "URL -> SHA-256 of each .deb to unpack.",
        ),
        "_fixup": attr.label(
            default = "//bazel/sysroot:fixup.sh",
            allow_single_file = True,
        ),
    },
)

_deb = tag_class(
    doc = "Declares a sysroot repository built from the packages in an apt archive.",
    attrs = {
        "arch": attr.string(
            mandatory = True,
            values = _MULTIARCH.keys(),
            doc = "dpkg architecture of the sysroot.",
        ),
        "build_file": attr.label(
            mandatory = True,
            doc = "BUILD file for the repository's root package; see `deb_sysroot`.",
        ),
        "components": attr.string_list(
            default = ["main"],
            doc = "Archive components to take packages from.",
        ),
        "exclude_packages": attr.string_list(
            doc = "Packages whose files and dependencies are left out. A trailing `*` matches a prefix.",
        ),
        "mirror": attr.string(
            mandatory = True,
            doc = "URL of the apt archive, the directory holding `dists/` and `pool/`.",
        ),
        "name": attr.string(
            mandatory = True,
            doc = "Name of the repository.",
        ),
        "packages": attr.string_list(
            mandatory = True,
            doc = "Packages to include; their dependencies are included too.",
        ),
        "suites": attr.string_list(
            mandatory = True,
            doc = "Archive suites to take packages from. Later suites take precedence.",
        ),
    },
)

def _sysroot_impl(module_ctx: module_ctx) -> extension_metadata:
    for mod in module_ctx.modules:
        for tag in mod.tags.deb:
            packages = {}
            provides = {}
            for suite in tag.suites:
                for component in tag.components:
                    index = "{}/{}/{}".format(tag.name, suite, component)
                    module_ctx.report_progress("Reading package index {} {}/{}".format(tag.arch, suite, component))
                    module_ctx.download_and_extract(
                        "{}/dists/{}/{}/binary-{}/Packages.gz".format(tag.mirror, suite, component, tag.arch),
                        output = index,
                        type = "gz",
                    )
                    _parse_index(module_ctx.read(index + "/Packages"), packages, provides)

            deb_sysroot(
                name = tag.name,
                arch = tag.arch,
                build_file = tag.build_file,
                debs = {
                    "{}/{}".format(tag.mirror, packages[name]["Filename"]): packages[name]["SHA256"]
                    for name in _closure(tag, packages, provides)
                },
            )
    return module_ctx.extension_metadata(
        root_module_direct_deps = "all",
        root_module_direct_dev_deps = [],
    )

sysroot = module_extension(
    implementation = _sysroot_impl,
    tag_classes = {"deb": _deb},
)
