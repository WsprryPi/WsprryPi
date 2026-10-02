#!/usr/bin/env python3
"""Build and atomically export one supported container target."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import uuid

TARGETS = {
    "armv6-bookworm": "linux/arm/v6",
    "armv6-trixie": "linux/arm/v6",
    "aarch64-bookworm": "linux/arm64",
    "aarch64-trixie": "linux/arm64",
}
REPO = Path(__file__).resolve().parent.parent
EVIDENCE = ("file.txt", "elf-header.txt", "elf-attributes.txt", "elf-versions.txt",
            "shared-libraries.txt", "build-packages.txt", "os-release.txt", "elf-dynamic.txt")


def check_destination(path):
    if path.is_symlink() or (path.exists() and
                            (not path.is_dir() or any(path.iterdir()))):
        raise ValueError(f"output must be an absent or empty directory: {path}")


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def build(target, output):
    # abspath preserves the final symlink so it can be rejected before use.
    output = Path(os.path.abspath(output))
    check_destination(output)
    platform = TARGETS[target]
    container = None
    staging = None
    try:
        with tempfile.TemporaryDirectory(prefix="wsprrypi-build-") as temporary:
            iidfile = Path(temporary) / "image-id"
            run("docker", "buildx", "build", "--file",
                str(REPO / "containers" / target / "Dockerfile"),
                "--platform", platform, "--target", "build", "--tag",
                f"wsprrypi-build:{target}", "--iidfile", str(iidfile),
                "--load", str(REPO))
            image = iidfile.read_text().strip()
            if not image.startswith("sha256:") or len(image) != 71:
                raise ValueError("Docker did not return a valid image digest")
            # Know our unique cleanup identity before create starts, including
            # when interrupted after Docker creates it but before stdout arrives.
            container = "wsprrypi-export-" + uuid.uuid4().hex
            run("docker", "create", "--name", container, "--platform", platform,
                image, stdout=subprocess.PIPE)
            output.parent.mkdir(parents=True, exist_ok=True)
            staging = Path(tempfile.mkdtemp(prefix=".wsprrypi-export-",
                                            dir=output.parent))
            run("docker", "cp", f"{container}:/artifact/.", str(staging))
            for name in EVIDENCE:
                if not (staging / name).is_file():
                    raise ValueError(f"export is missing evidence: {name}")
            binary = staging / "wsprrypi"
            expected = hashlib.sha256(binary.read_bytes()).hexdigest()
            if (staging / "SHA256SUMS").read_text().strip() != f"{expected}  wsprrypi":
                raise ValueError("exported executable checksum mismatch")
            cpu, release = target.split("-")
            arch = "armhf" if cpu == "armv6" else "arm64"
            if (staging / "target.txt").read_text() != (
                    f"cpu={cpu}\nrelease={release}\npackage_architecture={arch}\n"):
                raise ValueError("exported target does not match requested target")
            (staging / "build-image-id.txt").write_text(image + "\n")
            run("docker", "rm", container)
            container = None
            check_destination(output)
            # Same-filesystem rename replaces only an empty destination directory;
            # it cannot merge this artifact into another completed export.
            staging.chmod(0o755)
            os.rename(staging, output)
            staging = None
            print(f"{target} artifacts exported to {output}")
    finally:
        if container:
            subprocess.run(["docker", "rm", "-f", container], check=False)
        if staging:
            shutil.rmtree(staging)


def main():
    def interrupted(signum, frame):
        raise KeyboardInterrupt

    for signum in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=TARGETS)
    parser.add_argument("output", nargs="?")
    args = parser.parse_args()
    try:
        build(args.target, args.output or REPO / "dist" / args.target)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"container build failed: {error}\n")
    except KeyboardInterrupt:
        parser.exit(130, "container build interrupted\n")


if __name__ == "__main__":
    main()
