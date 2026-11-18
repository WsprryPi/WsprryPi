#!/usr/bin/env python3
"""Build the pinned PAKE-only SDK without installing into the host system.

This maintainer/build-time entry point does not enable application pairing. The
normal runtime has no source download, dependency repair, or provider selection.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

PIN_PATH = Path(__file__).with_name("pairing_provider.json")
MAX_ARCHIVE = 32 * 1024 * 1024
MAX_SOURCE = 128 * 1024 * 1024


def vendor_environment(source):
    # The verified archive has no Git metadata. Do not let upstream discover the
    # application's ancestor checkout and report its revision as Botan's.
    environment = os.environ.copy()
    for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE",
                "MAKEFLAGS", "MFLAGS", "MAKELEVEL"):
        environment.pop(key, None)
    environment["GIT_CEILING_DIRECTORIES"] = str(source.parent)
    return environment


def build_key(pin, compiler, cpu):
    toolchain = subprocess.check_output([compiler, "--version"], text=True, timeout=10)
    operating_system = Path("/etc/os-release")
    inputs = {"pin": pin, "recipe": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "compiler": compiler, "toolchain": toolchain, "cpu": cpu,
              "host": platform.system(), "machine": platform.machine(),
              "os_release": operating_system.read_text() if operating_system.exists() else platform.mac_ver()[0]}
    return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()[:24]


def verified_archive(cache, pin):
    archive = cache / (pin["sha256"] + ".tar.gz")
    if archive.is_symlink():
        raise ValueError("provider archive must not be a symlink")
    if not archive.exists():
        fd, temporary = tempfile.mkstemp(prefix="download-", dir=cache)
        try:
            with os.fdopen(fd, "wb") as output:
                with urllib.request.urlopen(pin["url"], timeout=30) as response:
                    if not response.geturl().startswith("https://"):
                        raise ValueError("provider download lost HTTPS")
                    total = 0
                    while chunk := response.read(65536):
                        total += len(chunk)
                        if total > MAX_ARCHIVE:
                            raise ValueError("provider archive exceeds bound")
                        output.write(chunk)
            if hashlib.sha256(Path(temporary).read_bytes()).hexdigest() != pin["sha256"]:
                raise ValueError("provider archive checksum mismatch")
            os.replace(temporary, archive)
        finally:
            Path(temporary).unlink(missing_ok=True)
    if archive.stat().st_size > MAX_ARCHIVE or hashlib.sha256(archive.read_bytes()).hexdigest() != pin["sha256"]:
        raise ValueError("provider archive checksum mismatch")
    return archive


def unpack(archive, destination):
    """Extract regular source files only; never follow archive links or paths."""
    total = 0
    seen = set()
    links = []
    top = None
    with tarfile.open(archive, "r:gz") as source:
        for member in source:
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or not path.parts:
                raise ValueError("unsafe provider archive path")
            if top is None:
                top = path.parts[0]
            if path.parts[0] != top:
                raise ValueError("multiple provider archive roots")
            if member.isdir():
                continue
            if len(path.parts) < 2:
                raise ValueError("provider archive contains a non-regular file")
            relative = Path(*path.parts[1:])
            if relative in seen:
                raise ValueError("duplicate provider archive path")
            seen.add(relative)
            if member.issym():
                target = PurePosixPath(posixpath.normpath(str(path.parent / member.linkname)))
                if target.is_absolute() or ".." in target.parts or target.parts[0] != top:
                    raise ValueError("unsafe provider archive link")
                links.append((relative, Path(*target.parts[1:])))
                continue
            if not member.isfile():
                raise ValueError("provider archive contains a non-regular file")
            total += member.size
            if total > MAX_SOURCE:
                raise ValueError("provider source exceeds bound")
            output = destination / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            with source.extractfile(member) as input_file, output.open("xb") as output_file:
                shutil.copyfileobj(input_file, output_file)
            output.chmod(0o755 if member.mode & 0o111 else 0o644)
    # Upstream's CI configuration is an internal symlink. Materialize links to
    # extracted regular files without creating a link or following link chains.
    for relative, target in links:
        if target in {entry[0] for entry in links} or not (destination / target).is_file():
            raise ValueError("provider archive link does not name a regular source file")
        total += (destination / target).stat().st_size
        if total > MAX_SOURCE:
            raise ValueError("provider source exceeds bound")
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        with (destination / target).open("rb") as input_file, output.open("xb") as output_file:
            shutil.copyfileobj(input_file, output_file)


def build(args):
    pin = json.loads(PIN_PATH.read_text())
    root = args.root.absolute()
    if root.is_symlink() or (root.exists() and any(root.iterdir())):
        raise ValueError("provider build root must be absent or empty")
    cache = args.cache.absolute()
    if cache.is_symlink():
        raise ValueError("provider cache must not be a symlink")
    cache.mkdir(parents=True, exist_ok=True)
    archive = verified_archive(cache, pin)
    root.mkdir(parents=True, exist_ok=True)
    source = root / "source"
    source.mkdir()
    unpack(archive, source)
    output = root / "build"
    stage = root / "sdk"
    command = [sys.executable, str(source / "configure.py"),
               "--with-build-dir=" + str(output), "--prefix=/usr",
               "--cc-bin=" + args.compiler,
               "--minimized-build", "--enable-modules=" + ",".join(pin["modules"]),
               "--disable-shared-library", "--without-documentation",
               "--build-targets=" + ("static,tests" if args.vectors else "static")]
    if args.cpu:
        command.append("--cpu=" + args.cpu)
    if args.cpu == "arm32" or (not args.cpu and platform.system() == "Linux" and
                               platform.machine().startswith("arm") and platform.machine() != "arm64"):
        command += ["--cpu=arm32"] if not args.cpu else []
        command.append("--cc-abi-flags=-march=armv6 -mfpu=vfp -mfloat-abi=hard")
    if args.cpu == "x86_64" and platform.system() == "Darwin":
        command.append("--cc-abi-flags=-arch x86_64")
    commands = [command, ["make", "-f", str(output / "Makefile"), "-j" + str(args.jobs)],
                ["make", "-f", str(output / "Makefile"), "install", "DESTDIR=" + str(stage)]]
    if args.vectors:
        test = [str(output / "botan-test"), "--data-dir=" + str(source / "src/tests/data"),
                "spake2p_kat", "spake2p_rt", "spake2p_password", "scrypt"]
        if args.cpu == "x86_64" and platform.machine() == "arm64":
            test = ["/usr/bin/arch", "-x86_64", *test]
        commands.append(test)
    for index, command in enumerate(commands):
        with (root / (str(index) + ".log")).open("w") as log:
            subprocess.run(command, cwd=source, env=vendor_environment(source), stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=1800)
        if index == 0:
            configured = json.loads((output / "build/build_config.json").read_text())
            if configured["version_vc_rev"] is not None:
                raise ValueError("archive provider inherited unrelated Git provenance")
            modules = configured["mod_list"]
            if not set(pin["modules"]) <= set(modules) or any(
                    module.startswith(("tls", "x509", "http", "asio")) for module in modules):
                raise ValueError("provider module closure violates PAKE-only profile")
    shutil.copyfile(source / "license.txt", stage / "BOTAN-LICENSE.txt")
    (stage / "provider.json").write_text(json.dumps({**pin, "enabled_modules": modules,
                                                  "commands": commands}, indent=2) + "\n")
    print(stage)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--key", action="store_true", help="Print the isolated source/toolchain build key")
    parser.add_argument("--compiler", default="c++", help="Compiler executable for the selected build environment")
    parser.add_argument("--cpu", choices=("arm32", "arm64", "x86_64"))
    parser.add_argument("--jobs", type=int, choices=range(1, 9), default=2)
    parser.add_argument("--vectors", action="store_true")
    args = parser.parse_args()
    try:
        if args.key:
            print(build_key(json.loads(PIN_PATH.read_text()), args.compiler, args.cpu))
            return 0
        if args.root is None or args.cache is None:
            parser.error("--root and --cache are required for a build")
        build(args)
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as error:
        print("Pairing provider build failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
