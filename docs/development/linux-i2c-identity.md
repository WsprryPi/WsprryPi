# Linux I2C adapter selection

The Si5351 component owns metadata-only stable identity discovery in
`src/WSPR-Transmitter/src/linux_i2c_identity.hpp`. Application configuration and
the production Linux transport share its resolver. The backend remains
`si5351`, and the transport is `linux-i2c`. Linux provides no direct USB HID implementation or backend fallback. The
separately selected [native macOS transport](macos-mcp2221a.md) uses the same
Si5351 backend without Linux device access.

## Configuration and identity

`Si5351.Adapter Identity` is a string. Empty retains the explicitly configured
numeric `I2C Bus` for generic adapters; MCP2221A requires a stable identity.
A nonempty identity takes precedence without rewriting the numeric setting.
INI, JSON, CLI `--si5351-adapter-identity`, runtime copies, and Pi endpoint
construction carry the same value. Shared Pi/Pico settings schemas, WTP wire
formats, and control versions are unchanged.

`--list-i2c-adapters` emits metadata without opening an adapter or probing a
slave. It reports each identity, current node, and matching error. Copy an
identity from that output or select its entry in the configuration UI.

Identity syntax is `usb:<vid>:<pid>:serial:<hex-encoded serial bytes>` or, only
when no serial is exposed, `usb:<vid>:<pid>:path:<hex-encoded canonical USB sysfs path>`.
Hex uses lowercase digits. Both forms bind VID/PID; serial identity must resolve
to exactly one I2C adapter. Duplicate serials are refused. Topology identity
binds this host and physical/controller topology, with no portability claim
across ports, controllers, or hosts. A newly exposed serial invalidates the old
topology identity. No serial is generated or rewritten.

Discovery walks `/sys/class/i2c-dev` and USB ancestors and requires a matching
character node. It performs no I2C ioctl or register transaction. Resolution
errors never choose the first match or substitute another bus.

## Access and lifetime

The production Linux adapter resolves the identity before open. It then checks
sysfs object identities and the opened character device's I2C major/minor,
queries `I2C_FUNCS`, requires `I2C_FUNC_I2C`, and selects the configured slave.
Before each transfer it resolves again and verifies the retained sysfs inode
binding and file descriptor. Detach/rebind and metadata changes invalidate the
session instead of reopening another device. This narrows discovery/use races;
it cannot turn userspace sysfs checks into an atomic kernel device lease.

Register reads retain the existing pointer-write/read sequence, with STOP
between syscalls. Exact transfer sizes remain mandatory; a partial transfer is
never retried as successful. Startup inhibition, cancellation, and cleanup use
the same selected transport. Close errors now fail startup or execution cleanup
and remain in the retained cleanup receipt. Disconnect can leave a separately
powered synthesizer generating output; software disable/readback is not measured
RF inactivity. Kernel transaction deadlines and physical recovery remain
host/kernel qualification requirements.

## Hardware-free validation

Use `.github/workflows/debian-non-hardware.yml` as the authoritative recipe.
Run ordinary tests unprivileged with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`, no
physical device passthrough, and explicit simulated backend selection for
application transmission tests. See [the simulator contract](../simulated-backend.md).

From `src`, run `make linux-i2c-identity-test linux-i2c-identity-config-test SUDO=`.
The first target is independently runnable from `src/WSPR-Transmitter/src`.
Retain Si5351 planner, fake-I2C transitions, startup-quiescence, full Linux
semantics, profile/factory/isolation, strict file-access audits, disposable
binary-only installation, RP1, and production network/USB companion coverage.
Run the UI's unit/browser suites and
`WSPRRYPI_I2C_EVIDENCE_DIR=<output> node tests/linux_i2c_identity_browser_test.js`
from `WsprryPi-UI` for desktop/mobile light/dark fixtures.

Use Ubuntu 24.04/x86-64/GCC 13 for the six warnings-as-errors profile builds in
the workflow. Identify emulation explicitly when running on an ARM host.
A local Debian 13 container can execute the canonical Debian recipe with its
listed packages. Keep each environment's Git/source snapshot and output trees
separate, record the base revision and tested uncommitted diff, and retain
commands/results. Containers share their Docker VM/host kernel; these checks do
not qualify Ubuntu USB binding, physical output, RF, or Raspberry Pi OS.

The reference hardware is [Adafruit MCP2221A Breakout — product 4471](https://www.adafruit.com/product/4471)
and [Adafruit Si5351A Clock Generator with STEMMA QT — product 5640](https://www.adafruit.com/product/5640),
with its 25 MHz crystal and address `0x60`. Physical acceptance remains a separate
gate under #411; native macOS acceptance belongs to #419.

## Local container recipe

Keep the validation checkout and outputs outside the operator checkout. Use a
clean ordinary clone of the exact candidate branch; its `.git` directory must
be self-contained. A worktree's `.git` file may point outside a container mount.
For uncommitted candidates, copy the reviewed changes into the isolated clone,
record a content-hash manifest, and refresh timestamps only for changed content
so older objects cannot mask a later correction.

A project-scoped Debian image can use this Dockerfile:

```dockerfile
FROM debian:trixie-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    age build-essential ca-certificates chromium cmake device-tree-compiler git \
    libgpiod-dev libssl-dev libavahi-client-dev libsystemd-dev nodejs npm php-cli \
    pkg-config python3 strace && rm -rf /var/lib/apt/lists/*
RUN useradd --create-home --uid 10001 validator && \
    install -d -o validator -g validator /work
USER validator
WORKDIR /work
ENV WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 npm_config_cache=/tmp/wsprrypi-validation-npm JOBS=2
```

From the directory containing that Dockerfile, build the image and bind the
isolated candidate clone read-only. Replace the source path with its verified
absolute location:

```sh
docker build -t wsprrypi-validation:debian13 .
docker run --rm \
  --mount type=bind,source=/absolute/path/to/candidate-clone,target=/source,readonly \
  wsprrypi-validation:debian13 sh -ec '
    cp -R /source/. /work/
    git log -1 --format="%H %D"
    cat /etc/os-release
    uname -m
    g++ --version
    cd src
    make debug SUDO=
    make semantics-test SUDO=
    make -C WSPR-Transmitter/src linux-i2c-identity-test si5351-planner-test si5351-transition-test startup-quiesce-test SUDO=
  '
```

These focused commands are an initial check. For complete acceptance, run every
`non-hardware-validation` step from the checked-in workflow in the copied
checkout, including its syscall traces and final hardware-access audit. Supply
the exact pinned RP1 reference and explicitly selected Pico/Mbed TLS checkouts
for the separate pair suites. Preserve logs outside a disposable container
before removing it. Use no `--privileged` or device passthrough.

For the Ubuntu compiler matrix, use `FROM ubuntu:24.04` in a separate image,
retain the common build packages above, and omit Chromium, age, and
`device-tree-compiler` when running only compiler/profile checks. Build and run
with `--platform linux/amd64` when x86-64 is required; record emulation on an
ARM host. In the isolated copy, use the exact `ubuntu-gcc13-release` workflow
recipe, including:

```sh
test "$(g++ -dumpversion | cut -d. -f1)" = 13
for profile in rpi-gpio rp1-gpclk si5351 simulated si5351,simulated rpi-gpio,rp1-gpclk,si5351,simulated
do
  make release backend-profile-factory-test BACKENDS="$profile" SUDO=
  omitted=
  case "$profile" in
    rpi-gpio|rp1-gpclk) omitted=si5351 ;;
    si5351) omitted=simulated ;;
    simulated) omitted=si5351 ;;
    si5351,simulated) omitted=gpio ;;
  esac
  ../scripts/tests/backend_capability_reporting_test.sh ./build/bin/wsprrypi "$profile" "$omitted"
  python3 ../scripts/tests/early_cli_exit_test.py ./build/bin/wsprrypi
done
python3 ../scripts/tests/logger_shutdown_test.py
```

The separate `strict-i2c-profile` job intentionally installs no libgpiod headers
and adds the unprivileged file-access and disposable binary-install audits.
Installing libgpiod for the default matrix does not substitute for that
no-header check. Capture OS/architecture, compiler and package versions, image
identity, candidate revision/manifest, commands, results, and limitations.
